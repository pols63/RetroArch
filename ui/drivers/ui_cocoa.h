/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
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

#ifndef _COCOA_UI
#define _COCOA_UI

#include <stdint.h>
#include <stddef.h>

#include <boolean.h>
#include <retro_common_api.h>

#include "../ui_companion_driver.h"

RETRO_BEGIN_DECLS

typedef struct ui_window_cocoa
{
    void *data;
} ui_window_cocoa_t;

/* "Import a Configuration File" / "Export a Configuration File" (Main
 * Menu > Configuration File) on macOS - see menu_cbs_ok.c's
 * action_ok_import_config / action_ok_export_config for the
 * cross-platform dispatch, and ui_cocoa.m for the implementation. Both
 * run modally and return synchronously; on success *out_path holds the
 * chosen absolute path. */
bool cocoa_show_config_import_dialog(char *out_path, size_t len);
bool cocoa_show_config_export_dialog(char *out_path, size_t len,
      const char *suggested_name);

RETRO_END_DECLS

#endif
