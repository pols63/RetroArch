/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

/* Bulk core backup to a user-selected SAF (Storage Access Framework)
 * folder on Android. See docs/retroarch-android-bulk-cores.md.
 *
 * The source is always the app's private core directory (dir_libretro) -
 * never user-selectable, per the design constraints in that doc. This is
 * a plain byte-for-byte copy of each installed core .so, not the
 * versioned/internal '.lcbk' backup format produced by
 * task_push_core_backup() (tasks/task_core_backup.c) - that function
 * targets a different feature (rollback history inside the app's own
 * storage) and its output isn't a readable core file. The chunked,
 * deadline-budgeted copy loop below mirrors the one in
 * task_core_restore_handler() (tasks/task_core_backup.c). */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <boolean.h>

#include <retro_miscellaneous.h>
#include <retro_dirent.h>
#include <string/stdstring.h>
#include <file/file_path.h>
#include <streams/interface_stream.h>
#include <features/features_cpu.h>

#include "../retroarch.h"
#include "../runloop.h"
#include "../verbosity.h"
#include "tasks_internal.h"

#if defined(ANDROID) && defined(HAVE_SAF)

#include <vfs/vfs_implementation_saf.h>

#define CORE_BULK_BACKUP_SUFFIX_ANDROID "_libretro_android.so"
#define CORE_BULK_BACKUP_SUFFIX_PLAIN   "_libretro.so"

/* Same quantum/budget as task_core_backup.c's CORE_BACKUP_CHUNK_SIZE /
 * CORE_BACKUP_TICK_BUDGET_US - kept local since those are not exported. */
#define CORE_BULK_BACKUP_CHUNK_SIZE      (100 * 1024)
#define CORE_BULK_BACKUP_TICK_BUDGET_US  2000

typedef struct
{
   char    *filename; /* basename only */
   int64_t  size;
} core_bulk_backup_entry_t;

static core_bulk_backup_entry_t *core_bulk_backup_pending        = NULL;
static size_t                    core_bulk_backup_pending_size   = 0;
static uint64_t                  core_bulk_backup_pending_total  = 0;
static char                     *core_bulk_backup_pending_src    = NULL; /* dir_libretro */
static char                     *core_bulk_backup_pending_dst    = NULL; /* raw saf tree */
static bool                      core_bulk_backup_task_active    = false;

enum core_bulk_backup_status
{
   CORE_BULK_BACKUP_OPEN = 0,
   CORE_BULK_BACKUP_ITERATE,
   CORE_BULK_BACKUP_SUMMARY,
   CORE_BULK_BACKUP_END
};

typedef struct
{
   char *dir_libretro;
   char *saf_dest_tree;
   core_bulk_backup_entry_t *entries;
   size_t num_entries;
   size_t current_index;

   intfstream_t *src_file;
   intfstream_t *dst_file;
   uint8_t *buffer;

   size_t num_success;
   size_t num_failed;
   char *failed_names;
   size_t failed_names_len;
   size_t failed_names_cap;

   enum core_bulk_backup_status status;
} core_bulk_backup_handle_t;

static bool core_bulk_backup_filename_is_core(const char *name)
{
   size_t len;
   if (!name || !*name)
      return false;
   len = strlen(name);
   return    string_ends_with_size(name, CORE_BULK_BACKUP_SUFFIX_ANDROID,
                  len, STRLEN_CONST(CORE_BULK_BACKUP_SUFFIX_ANDROID))
          || string_ends_with_size(name, CORE_BULK_BACKUP_SUFFIX_PLAIN,
                  len, STRLEN_CONST(CORE_BULK_BACKUP_SUFFIX_PLAIN));
}

static void core_bulk_backup_pending_clear(void)
{
   size_t i;

   for (i = 0; i < core_bulk_backup_pending_size; i++)
      free(core_bulk_backup_pending[i].filename);

   free(core_bulk_backup_pending);
   core_bulk_backup_pending       = NULL;
   core_bulk_backup_pending_size  = 0;
   core_bulk_backup_pending_total = 0;

   free(core_bulk_backup_pending_src);
   core_bulk_backup_pending_src   = NULL;

   free(core_bulk_backup_pending_dst);
   core_bulk_backup_pending_dst   = NULL;
}

size_t core_bulk_backup_scan(const char *dir_libretro, const char *saf_dest_tree)
{
   struct RDIR *rdir;

   core_bulk_backup_pending_clear();

   if (!dir_libretro || !*dir_libretro || !saf_dest_tree || !*saf_dest_tree)
      return 0;

   if (!(rdir = retro_opendir(dir_libretro)))
      return 0;

   while (retro_readdir(rdir))
   {
      const char *name = retro_dirent_get_name(rdir);
      char full_path[PATH_MAX_LENGTH];
      core_bulk_backup_entry_t *new_array;
      int64_t size;

      if (retro_dirent_is_dir(rdir, NULL))
         continue;

      if (!core_bulk_backup_filename_is_core(name))
         continue;

      fill_pathname_join_special(full_path, dir_libretro, name, sizeof(full_path));
      size = path_get_size(full_path);

      new_array = (core_bulk_backup_entry_t*)realloc(core_bulk_backup_pending,
            (core_bulk_backup_pending_size + 1) * sizeof(*new_array));

      if (!new_array)
         continue;

      core_bulk_backup_pending = new_array;

      core_bulk_backup_pending[core_bulk_backup_pending_size].filename = strdup(name);
      core_bulk_backup_pending[core_bulk_backup_pending_size].size     = size;

      if (size > 0)
         core_bulk_backup_pending_total += (uint64_t)size;

      core_bulk_backup_pending_size++;
   }

   retro_closedir(rdir);

   if (core_bulk_backup_pending_size > 0)
   {
      core_bulk_backup_pending_src = strdup(dir_libretro);
      core_bulk_backup_pending_dst = strdup(saf_dest_tree);
   }

   RARCH_LOG("[Core Bulk Backup] Scanned core directory: %u core file(s), ~%llu bytes total.\n",
         (unsigned)core_bulk_backup_pending_size,
         (unsigned long long)core_bulk_backup_pending_total);

   return core_bulk_backup_pending_size;
}

size_t core_bulk_backup_pending_count(void)
{
   return core_bulk_backup_pending_size;
}

uint64_t core_bulk_backup_pending_total_size(void)
{
   return core_bulk_backup_pending_total;
}

const char *core_bulk_backup_pending_filename(size_t idx)
{
   if (idx >= core_bulk_backup_pending_size)
      return NULL;
   return core_bulk_backup_pending[idx].filename;
}

void core_bulk_backup_cancel_pending(void)
{
   core_bulk_backup_pending_clear();
}

static void core_bulk_backup_append_failed_name(core_bulk_backup_handle_t *h,
      const char *name)
{
   size_t name_len;
   size_t needed;

   if (!name)
      return;

   name_len = strlen(name);
   needed   = h->failed_names_len + name_len + 3;

   if (needed > h->failed_names_cap)
   {
      size_t new_cap = h->failed_names_cap ? h->failed_names_cap * 2 : 128;
      char  *new_buf;

      while (new_cap < needed)
         new_cap *= 2;

      if (!(new_buf = (char*)realloc(h->failed_names, new_cap)))
         return;

      if (!h->failed_names)
         new_buf[0] = '\0';

      h->failed_names     = new_buf;
      h->failed_names_cap = new_cap;
   }

   if (h->failed_names_len > 0)
      h->failed_names_len += strlcpy(h->failed_names + h->failed_names_len,
            ", ", h->failed_names_cap - h->failed_names_len);

   h->failed_names_len += strlcpy(h->failed_names + h->failed_names_len,
         name, h->failed_names_cap - h->failed_names_len);
}

static void core_bulk_backup_close_files(core_bulk_backup_handle_t *h)
{
   if (h->src_file)
   {
      intfstream_close(h->src_file);
      free(h->src_file);
      h->src_file = NULL;
   }

   if (h->dst_file)
   {
      intfstream_flush(h->dst_file);
      intfstream_close(h->dst_file);
      free(h->dst_file);
      h->dst_file = NULL;
   }
}

static void core_bulk_backup_free_handle(core_bulk_backup_handle_t *h)
{
   size_t i;

   if (!h)
      return;

   core_bulk_backup_close_files(h);

   for (i = 0; i < h->num_entries; i++)
      free(h->entries[i].filename);

   free(h->entries);
   free(h->dir_libretro);
   free(h->saf_dest_tree);
   free(h->failed_names);
   free(h->buffer);
   free(h);

   core_bulk_backup_task_active = false;
}

static void task_core_bulk_backup_handler(retro_task_t *task)
{
   core_bulk_backup_handle_t *h = (core_bulk_backup_handle_t*)task->state;

   if (!h)
      goto task_finished;

   switch (h->status)
   {
      case CORE_BULK_BACKUP_OPEN:
      {
         const core_bulk_backup_entry_t *entry;
         char src_path[PATH_MAX_LENGTH];
         char *dst_path;

         if (h->current_index >= h->num_entries)
         {
            h->status = CORE_BULK_BACKUP_SUMMARY;
            break;
         }

         entry = &h->entries[h->current_index];

         RARCH_LOG("[Core Bulk Backup] Copying \"%s\"...\n", entry->filename);

         fill_pathname_join_special(src_path, h->dir_libretro,
               entry->filename, sizeof(src_path));

         h->src_file = intfstream_open_file(src_path,
               RETRO_VFS_FILE_ACCESS_READ, RETRO_VFS_FILE_ACCESS_HINT_NONE);

         dst_path = retro_vfs_path_join_saf(h->saf_dest_tree, entry->filename);

         if (dst_path)
         {
            h->dst_file = intfstream_open_file(dst_path,
                  RETRO_VFS_FILE_ACCESS_WRITE, RETRO_VFS_FILE_ACCESS_HINT_NONE);
            free(dst_path);
         }

         if (!h->src_file || !h->dst_file)
         {
            RARCH_ERR("[Core Bulk Backup] Failed to open \"%s\" for copy.\n",
                  entry->filename);
            core_bulk_backup_close_files(h);
            h->num_failed++;
            core_bulk_backup_append_failed_name(h, entry->filename);
            h->current_index++;
            break;
         }

         if (!h->buffer)
            h->buffer = (uint8_t*)malloc(CORE_BULK_BACKUP_CHUNK_SIZE);

         if (!h->buffer)
         {
            RARCH_ERR("[Core Bulk Backup] Failed to allocate transfer buffer.\n");
            core_bulk_backup_close_files(h);
            h->num_failed++;
            core_bulk_backup_append_failed_name(h, entry->filename);
            h->current_index++;
            break;
         }

         h->status = CORE_BULK_BACKUP_ITERATE;
      }
      break;

      case CORE_BULK_BACKUP_ITERATE:
      {
         const core_bulk_backup_entry_t *entry = &h->entries[h->current_index];
         retro_time_t deadline = cpu_features_get_time_usec()
               + CORE_BULK_BACKUP_TICK_BUDGET_US;
         bool failed        = false;
         bool reached_eof    = false;
         int64_t data_read;

         do
         {
            data_read = intfstream_read(h->src_file, h->buffer, CORE_BULK_BACKUP_CHUNK_SIZE);

            if (data_read < 0)
            {
               failed = true;
               break;
            }

            if (data_read == 0)
            {
               reached_eof = true;
               break;
            }

            if (intfstream_write(h->dst_file, h->buffer, data_read) != data_read)
            {
               failed = true;
               break;
            }
         } while (cpu_features_get_time_usec() < deadline);

         if (failed)
         {
            RARCH_ERR("[Core Bulk Backup] I/O error copying \"%s\".\n", entry->filename);
            core_bulk_backup_close_files(h);
            h->num_failed++;
            core_bulk_backup_append_failed_name(h, entry->filename);
            h->current_index++;
            h->status = CORE_BULK_BACKUP_OPEN;
            break;
         }

         if (reached_eof)
         {
            core_bulk_backup_close_files(h);
            RARCH_LOG("[Core Bulk Backup] OK: \"%s\"\n", entry->filename);
            h->num_success++;
            h->current_index++;
            h->status = CORE_BULK_BACKUP_OPEN;
         }

         task_set_progress(task, (int8_t)
               ((h->current_index * 100) / (h->num_entries ? h->num_entries : 1)));
      }
      break;

      case CORE_BULK_BACKUP_SUMMARY:
      {
         char msg[512];
         size_t _len = (size_t)snprintf(msg, sizeof(msg),
               "Core backup: %u copied, %u failed",
               (unsigned)h->num_success, (unsigned)h->num_failed);

         if (h->num_failed > 0 && h->failed_names && _len < sizeof(msg))
         {
            _len += strlcpy(msg + _len, " (", sizeof(msg) - _len);
            _len += strlcpy(msg + _len, h->failed_names, sizeof(msg) - _len);
            _len += strlcpy(msg + _len, ")", sizeof(msg) - _len);
         }

         runloop_msg_queue_push(msg, _len, 1, 180, true, NULL,
               MESSAGE_QUEUE_ICON_DEFAULT, MESSAGE_QUEUE_CATEGORY_INFO);

         h->status = CORE_BULK_BACKUP_END;
      }
      break;

      default:
         task_set_progress(task, 100);
         goto task_finished;
   }

   return;

task_finished:

   if (task)
      task_set_flags(task, RETRO_TASK_FLG_FINISHED, true);

   core_bulk_backup_free_handle(h);
}

bool task_push_core_bulk_backup(void)
{
   retro_task_t *task           = NULL;
   core_bulk_backup_handle_t *h = NULL;

   if (core_bulk_backup_task_active || core_bulk_backup_pending_size == 0)
      return false;

   if (!(h = (core_bulk_backup_handle_t*)calloc(1, sizeof(*h))))
      return false;

   if (!(task = task_init()))
   {
      free(h);
      return false;
   }

   h->entries       = core_bulk_backup_pending;
   h->num_entries   = core_bulk_backup_pending_size;
   h->dir_libretro  = core_bulk_backup_pending_src;
   h->saf_dest_tree = core_bulk_backup_pending_dst;

   core_bulk_backup_pending       = NULL;
   core_bulk_backup_pending_size  = 0;
   core_bulk_backup_pending_total = 0;
   core_bulk_backup_pending_src   = NULL;
   core_bulk_backup_pending_dst   = NULL;

   task->handler     = task_core_bulk_backup_handler;
   task->state       = h;
   task->title       = strdup("Backing up cores...");
   task->progress    = 0;
   task->progress_cb = task_window_progress_cb;
   task->flags      |= RETRO_TASK_FLG_ALTERNATIVE_LOOK;

   core_bulk_backup_task_active = true;

   task_queue_push(task);

   return true;
}

#endif /* defined(ANDROID) && defined(HAVE_SAF) */
