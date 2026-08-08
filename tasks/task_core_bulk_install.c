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

/* Bulk core install from a user-selected SAF (Storage Access Framework)
 * folder on Android. See docs/retroarch-android-bulk-cores.md.
 *
 * This deliberately does not reimplement core installation: every file
 * is installed via the existing task_push_core_restore() (tasks/task_core_backup.c),
 * exactly as the single-file "Install or Restore a Core" menu entry does.
 * This file only adds directory scanning and per-file sequencing on top. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <boolean.h>

#include <retro_miscellaneous.h>
#include <retro_dirent.h>
#include <string/stdstring.h>
#include <file/file_path.h>

#include "../command.h"
#include "../configuration.h"
#include "../retroarch.h"
#include "../runloop.h"
#include "../verbosity.h"
#include "../file_path_special.h"
#include "tasks_internal.h"

#if defined(ANDROID) && defined(HAVE_SAF)

#include <vfs/vfs_implementation_saf.h>

/* Matches the Android core filename convention used elsewhere in this
 * codebase (see play_feature_delivery.c and InstalledCoresReceiver.java) */
#define CORE_BULK_SUFFIX_ANDROID  "_libretro_android.so"
#define CORE_BULK_SUFFIX_PLAIN    "_libretro.so"

typedef struct
{
   char *filename;      /* basename only, for display/log/summary */
   char *saf_path;       /* full "saf://tree/..." path, source for install */
   bool is_overwrite;    /* true if a core with this filename is already installed */
} core_bulk_install_entry_t;

/* Pending scan (populated by core_bulk_install_scan, consumed by
 * task_push_core_bulk_install). Only one scan/operation is supported
 * at a time - a second scan replaces the first, matching the "single
 * batch confirmation" requirement (no concurrent bulk operations). */
static core_bulk_install_entry_t *core_bulk_install_pending          = NULL;
static size_t                     core_bulk_install_pending_size     = 0;
static size_t                     core_bulk_install_overwrite_size   = 0;
static char                      *core_bulk_install_pending_dir      = NULL;
/* Full "saf://tree/..." path of an "info.zip" found alongside the core
 * files in the scanned folder, or NULL if none was present. See the
 * CORE_BULK_INSTALL_INFO_ZIP state below. */
static char                      *core_bulk_install_pending_info_zip = NULL;
static bool                       core_bulk_install_task_active      = false;

enum core_bulk_install_status
{
   CORE_BULK_INSTALL_NEXT = 0,
   CORE_BULK_INSTALL_WAIT,
   CORE_BULK_INSTALL_INFO_ZIP,
   CORE_BULK_INSTALL_INFO_ZIP_WAIT,
   CORE_BULK_INSTALL_SUMMARY,
   CORE_BULK_INSTALL_END
};

typedef struct
{
   char *dir_libretro;
   core_bulk_install_entry_t *entries;
   size_t num_entries;
   size_t current_index;
   size_t num_success;
   size_t num_failed;
   char *failed_names;
   size_t failed_names_len;
   size_t failed_names_cap;
   /* Owns the "saf://..." path of a pending "info.zip", if any -
    * handed over from core_bulk_install_pending_info_zip at push time,
    * same as 'entries' is handed over from core_bulk_install_pending. */
   char *info_zip_saf_path;
   bool info_zip_updated;
   enum core_bulk_install_status status;
} core_bulk_install_handle_t;

static bool core_bulk_filename_is_core(const char *name)
{
   size_t len;
   if (!name || !*name)
      return false;
   len = strlen(name);
   return    string_ends_with_size(name, CORE_BULK_SUFFIX_ANDROID,
                  len, STRLEN_CONST(CORE_BULK_SUFFIX_ANDROID))
          || string_ends_with_size(name, CORE_BULK_SUFFIX_PLAIN,
                  len, STRLEN_CONST(CORE_BULK_SUFFIX_PLAIN));
}

static void core_bulk_install_pending_clear(void)
{
   size_t i;

   for (i = 0; i < core_bulk_install_pending_size; i++)
   {
      free(core_bulk_install_pending[i].filename);
      free(core_bulk_install_pending[i].saf_path);
   }

   free(core_bulk_install_pending);
   core_bulk_install_pending        = NULL;
   core_bulk_install_pending_size   = 0;
   core_bulk_install_overwrite_size = 0;

   free(core_bulk_install_pending_dir);
   core_bulk_install_pending_dir    = NULL;

   free(core_bulk_install_pending_info_zip);
   core_bulk_install_pending_info_zip = NULL;
}

size_t core_bulk_install_scan(const char *saf_tree, const char *dir_libretro)
{
   struct RDIR *rdir;
   char *root_path;

   core_bulk_install_pending_clear();

   if (!saf_tree || !*saf_tree || !dir_libretro || !*dir_libretro)
      return 0;

   if (!(root_path = retro_vfs_path_join_saf(saf_tree, "")))
      return 0;

   rdir = retro_opendir(root_path);
   free(root_path);

   if (!rdir)
      return 0;

   core_bulk_install_pending_dir = strdup(dir_libretro);

   while (retro_readdir(rdir))
   {
      const char *name = retro_dirent_get_name(rdir);
      char *file_saf_path;
      char dest_path[PATH_MAX_LENGTH];
      core_bulk_install_entry_t *new_array;

      if (retro_dirent_is_dir(rdir, NULL))
         continue;

      /* "info.zip" alongside the core files: same name the online
       * "Update Core Info Files" updater downloads (FILE_PATH_CORE_INFO_ZIP,
       * file_path_special.h). Not a core file, so it is not added to
       * core_bulk_install_pending - it is installed separately, see
       * CORE_BULK_INSTALL_INFO_ZIP in task_core_bulk_install_handler(). */
      if (string_is_equal_noncase(name, FILE_PATH_CORE_INFO_ZIP))
      {
         free(core_bulk_install_pending_info_zip);
         core_bulk_install_pending_info_zip = retro_vfs_path_join_saf(saf_tree, name);
         continue;
      }

      if (!core_bulk_filename_is_core(name))
         continue;

      if (!(file_saf_path = retro_vfs_path_join_saf(saf_tree, name)))
         continue;

      new_array = (core_bulk_install_entry_t*)realloc(core_bulk_install_pending,
            (core_bulk_install_pending_size + 1) * sizeof(*new_array));

      if (!new_array)
      {
         free(file_saf_path);
         continue;
      }

      core_bulk_install_pending = new_array;

      fill_pathname_join_special(dest_path, dir_libretro, name, sizeof(dest_path));

      core_bulk_install_pending[core_bulk_install_pending_size].filename     = strdup(name);
      core_bulk_install_pending[core_bulk_install_pending_size].saf_path     = file_saf_path;
      core_bulk_install_pending[core_bulk_install_pending_size].is_overwrite = path_is_valid(dest_path);

      if (core_bulk_install_pending[core_bulk_install_pending_size].is_overwrite)
         core_bulk_install_overwrite_size++;

      core_bulk_install_pending_size++;
   }

   retro_closedir(rdir);

   RARCH_LOG("[Core Bulk Install] Scanned SAF folder: %u core file(s) found, %u would overwrite an installed core, info.zip %sfound.\n",
         (unsigned)core_bulk_install_pending_size, (unsigned)core_bulk_install_overwrite_size,
         core_bulk_install_pending_info_zip ? "" : "not ");

   return core_bulk_install_pending_size;
}

bool core_bulk_install_pending_has_info_zip(void)
{
   return core_bulk_install_pending_info_zip != NULL;
}

size_t core_bulk_install_pending_count(void)
{
   return core_bulk_install_pending_size;
}

size_t core_bulk_install_pending_overwrite_count(void)
{
   return core_bulk_install_overwrite_size;
}

const char *core_bulk_install_pending_filename(size_t idx)
{
   if (idx >= core_bulk_install_pending_size)
      return NULL;
   return core_bulk_install_pending[idx].filename;
}

bool core_bulk_install_pending_is_overwrite(size_t idx)
{
   if (idx >= core_bulk_install_pending_size)
      return false;
   return core_bulk_install_pending[idx].is_overwrite;
}

void core_bulk_install_cancel_pending(void)
{
   core_bulk_install_pending_clear();
}

static void core_bulk_install_append_failed_name(core_bulk_install_handle_t *h,
      const char *name)
{
   size_t name_len;
   size_t needed;

   if (!name)
      return;

   name_len = strlen(name);
   /* separator (", ") + name + NUL */
   needed   = h->failed_names_len + name_len + 3;

   if (needed > h->failed_names_cap)
   {
      size_t new_cap  = h->failed_names_cap ? h->failed_names_cap * 2 : 128;
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

static void core_bulk_install_free_handle(core_bulk_install_handle_t *h)
{
   size_t i;

   if (!h)
      return;

   for (i = 0; i < h->num_entries; i++)
   {
      free(h->entries[i].filename);
      free(h->entries[i].saf_path);
   }

   free(h->entries);
   free(h->dir_libretro);
   free(h->failed_names);
   free(h->info_zip_saf_path);
   free(h);

   core_bulk_install_task_active = false;
}

/* Finish callback for the "info.zip" -> path_libretro_info decompress
 * task pushed from CORE_BULK_INSTALL_INFO_ZIP below. Mirrors what
 * cb_decompressed() (menu/cbs/menu_cbs_ok.c) does for the online
 * "Update Core Info Files" download in the MENU_ENUM_LABEL_CB_UPDATE_CORE_INFO_FILES
 * case - forces a core-info rescan so the newly-extracted .info files
 * are picked up - except it must NOT delete the source file the way
 * that callback does: there the source is a temp download, here it is
 * the user's own info.zip on their chosen SAF folder.
 *
 * Runs on the main thread (all retro_task_t callbacks do, once the
 * task is retired), so calling command_event() here - unlike from this
 * file's own task handler, which runs on the task worker thread - does
 * not race the core-info list rebuild it triggers. */
static void cb_core_bulk_install_info_zip(retro_task_t *task,
      void *task_data, void *user_data, const char *err)
{
   decompress_task_data_t *dec = (decompress_task_data_t*)task_data;

   if (dec && !err)
   {
      bool refresh = true;
      command_event(CMD_EVENT_CORE_INFO_INIT, &refresh);
   }

   if (err)
      RARCH_ERR("[Core Bulk Install] Failed to extract \"%s\": %s\n",
            FILE_PATH_CORE_INFO_ZIP, err);

   if (dec)
   {
      free(dec->source_file);
      free(dec);
   }
}

static void task_core_bulk_install_handler(retro_task_t *task)
{
   core_bulk_install_handle_t *h = (core_bulk_install_handle_t*)task->state;

   if (!h)
      goto task_finished;

   switch (h->status)
   {
      case CORE_BULK_INSTALL_NEXT:
      {
         const core_bulk_install_entry_t *entry;
         bool core_loaded  = false;
         char task_title[128];

         if (h->current_index >= h->num_entries)
         {
            h->status = CORE_BULK_INSTALL_INFO_ZIP;
            break;
         }

         entry = &h->entries[h->current_index];

         RARCH_LOG("[Core Bulk Install] Installing \"%s\"...\n", entry->filename);

         /* Single on-screen progress display for the whole batch:
          * the per-file restore task below is pushed muted
          * (RETRO_TASK_FLG_MUTE) so its own progress/toast never
          * shows, and this task's title is updated here instead -
          * otherwise both this task and the per-file restore task
          * are ALTERNATIVE_LOOK tasks progressing at the same time,
          * and their titles fight over the same on-screen slot. */
         task_free_title(task);
         snprintf(task_title, sizeof(task_title), "Installing cores... (%u/%u) %s",
               (unsigned)(h->current_index + 1), (unsigned)h->num_entries,
               entry->filename);
         task_set_title(task, strdup(task_title));

         /* Passing the filename as 'core_display_name' keeps
          * task_push_core_restore() from calling core_info_find():
          * this handler runs on the task worker thread (not the
          * main thread), and that call reads global core-info state
          * that a previous file's finish callback may be
          * concurrently rebuilding via CMD_EVENT_CORE_INFO_INIT on
          * the main thread - see the NOTE on the prototype in
          * tasks_internal.h. The filename is only used for transient
          * task-title/log/error text here; the menu's own core list
          * picks up the proper display name once core info is
          * (re-)scanned. */
         if (task_push_core_restore(entry->saf_path, h->dir_libretro,
                  entry->filename, &core_loaded, NULL, true))
         {
            h->status = CORE_BULK_INSTALL_WAIT;
         }
         else
         {
            /* Rejected synchronously (invalid file, locked core, or a
             * restore already queued for this exact core path) */
            RARCH_LOG("[Core Bulk Install] Rejected: \"%s\"\n", entry->filename);
            h->num_failed++;
            core_bulk_install_append_failed_name(h, entry->filename);
            h->current_index++;
         }

         task_set_progress(task, (int8_t)
               ((h->current_index * 100) / (h->num_entries ? h->num_entries : 1)));
      }
      break;

      case CORE_BULK_INSTALL_WAIT:
      {
         const core_bulk_install_entry_t *entry = &h->entries[h->current_index];
         char dest_path[PATH_MAX_LENGTH];

         fill_pathname_join_special(dest_path, h->dir_libretro,
               entry->filename, sizeof(dest_path));

         /* Polling task_queue_find() rather than retaining the
          * retro_task_t* the restore call handed back: that task can
          * finish and be retired (callback run, then freed) from the
          * main thread at any time once it is done, and this handler
          * runs on the task worker thread - there is no lock that
          * keeps such a free from racing a direct task_get_flags()
          * poll on a stashed pointer. task_queue_find() only ever
          * reports true/false under its own locks, so it is safe to
          * poll here regardless of which thread retires the task. */
         if (task_core_backup_find(dest_path))
            break;

         /* No longer reachable via the task queue - it fully
          * finished and retired. task_push_core_restore() does not
          * report per-file async success/failure to the caller, so a
          * completed install is inferred from the destination file
          * now existing in the private core directory. The native
          * per-file toast (installed/failed) is still shown by
          * task_push_core_restore()'s own task regardless. */
         if (path_is_valid(dest_path))
         {
            RARCH_LOG("[Core Bulk Install] OK: \"%s\"\n", entry->filename);
            h->num_success++;
         }
         else
         {
            RARCH_LOG("[Core Bulk Install] Failed: \"%s\"\n", entry->filename);
            h->num_failed++;
            core_bulk_install_append_failed_name(h, entry->filename);
         }

         h->current_index++;
         h->status = CORE_BULK_INSTALL_NEXT;
      }
      break;

      case CORE_BULK_INSTALL_INFO_ZIP:
      {
         settings_t *settings;
         const char *dir_info;

         if (!h->info_zip_saf_path)
         {
            h->status = CORE_BULK_INSTALL_SUMMARY;
            break;
         }

         settings = config_get_ptr();
         dir_info = settings->paths.path_libretro_info;

         RARCH_LOG("[Core Bulk Install] Updating core info database from \"%s\"...\n",
               FILE_PATH_CORE_INFO_ZIP);

         /* Reuses the exact same extraction task the online "Update
          * Core Info Files" updater pushes for its downloaded
          * info.zip (menu/cbs/menu_cbs_ok.c, cb_generic_download) -
          * this is the offline equivalent of that download, so it
          * runs the same install step rather than reimplementing zip
          * extraction here. The VFS layer opens "saf://" sources
          * transparently (same as task_push_core_restore() above),
          * so the SAF path is passed straight through. */
         if (   dir_info && *dir_info
             && task_push_decompress(h->info_zip_saf_path, dir_info,
                   NULL, NULL, NULL, cb_core_bulk_install_info_zip,
                   NULL, NULL, true))
            h->status = CORE_BULK_INSTALL_INFO_ZIP_WAIT;
         else
         {
            RARCH_LOG("[Core Bulk Install] Failed to start core info database update.\n");
            h->status = CORE_BULK_INSTALL_SUMMARY;
         }
      }
      break;

      case CORE_BULK_INSTALL_INFO_ZIP_WAIT:
      {
         /* Same safe polling as CORE_BULK_INSTALL_WAIT above - never
          * touch the retro_task_t* task_push_decompress() would have
          * handed back, only ask the queue whether it is still
          * reachable. */
         if (task_check_decompress(h->info_zip_saf_path))
            break;

         /* No longer reachable: fully retired, which - per
          * cb_core_bulk_install_info_zip() above - means the core
          * info rescan has already run if extraction succeeded. */
         h->info_zip_updated = true;
         h->status           = CORE_BULK_INSTALL_SUMMARY;
      }
      break;

      case CORE_BULK_INSTALL_SUMMARY:
      {
         char msg[512];
         size_t _len = (size_t)snprintf(msg, sizeof(msg),
               "Bulk core install: %u installed, %u failed",
               (unsigned)h->num_success, (unsigned)h->num_failed);

         if (h->num_failed > 0 && h->failed_names && _len < sizeof(msg))
         {
            _len += strlcpy(msg + _len, " (", sizeof(msg) - _len);
            _len += strlcpy(msg + _len, h->failed_names, sizeof(msg) - _len);
            _len += strlcpy(msg + _len, ")", sizeof(msg) - _len);
         }

         if (h->info_zip_saf_path && _len < sizeof(msg))
            _len += strlcpy(msg + _len,
                  h->info_zip_updated
                        ? "; core info database updated"
                        : "; core info database update failed",
                  sizeof(msg) - _len);

         runloop_msg_queue_push(msg, _len, 1, 180, true, NULL,
               MESSAGE_QUEUE_ICON_DEFAULT, MESSAGE_QUEUE_CATEGORY_INFO);

         h->status = CORE_BULK_INSTALL_END;
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

   core_bulk_install_free_handle(h);
}

bool task_push_core_bulk_install(void)
{
   retro_task_t *task            = NULL;
   core_bulk_install_handle_t *h = NULL;

   if (   core_bulk_install_task_active
       || (core_bulk_install_pending_size == 0 && !core_bulk_install_pending_info_zip))
      return false;

   if (!(h = (core_bulk_install_handle_t*)calloc(1, sizeof(*h))))
      return false;

   if (!(task = task_init()))
   {
      free(h);
      return false;
   }

   /* Hand ownership of the pending scan over to the task */
   h->entries           = core_bulk_install_pending;
   h->num_entries       = core_bulk_install_pending_size;
   h->dir_libretro      = core_bulk_install_pending_dir;
   h->info_zip_saf_path = core_bulk_install_pending_info_zip;

   core_bulk_install_pending          = NULL;
   core_bulk_install_pending_size     = 0;
   core_bulk_install_overwrite_size   = 0;
   core_bulk_install_pending_dir      = NULL;
   core_bulk_install_pending_info_zip = NULL;

   task->handler     = task_core_bulk_install_handler;
   task->state       = h;
   task->title       = strdup("Installing cores...");
   task->progress    = 0;
   task->progress_cb = task_window_progress_cb;
   task->flags      |= RETRO_TASK_FLG_ALTERNATIVE_LOOK;

   core_bulk_install_task_active = true;

   task_queue_push(task);

   return true;
}

#endif /* defined(ANDROID) && defined(HAVE_SAF) */
