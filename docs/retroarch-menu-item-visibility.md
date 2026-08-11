# Cómo activar/desactivar la visibilidad de secciones del menú

RetroArch trae un sistema nativo de 76 interruptores para ocultar partes
del menú sin tocar código — pestañas del menú principal, entradas del
menú principal, categorías enteras de Settings, e ítems del Quick Menu
(el menú in-game). Todos se guardan en `retroarch.cfg` igual que
cualquier otro ajuste. Este doc cubre las 3 formas de tocarlos y trae la
tabla de referencia completa con las claves reales verificadas en el
código fuente (algunas difieren del nombre de la variable interna, ver
notas).

## Método 1 — Desde el menú (interactivo, por instalación)

- **Categorías completas de Settings** (ocultar toda la sección
  "Achievements", "Network", "Steam", etc.): `Settings → User Interface →
  Settings Views`.
- **Ítems del Quick Menu** (el menú que aparece en pausa durante una
  partida): `Settings → User Interface → Quick Menu Views`.
- **Pestañas del menú principal** (Historial, Favoritos, Imágenes,
  Explorar, etc.) y **entradas del menú principal** (Load Core,
  Information, Help, Quit, etc.): viven bajo `Settings → User Interface →
  Menu` en la mayoría de los drivers, o `Settings → Appearance` según el
  driver activo (RGUI/XMB/Ozone/MaterialUI organizan esto distinto). Si no
  lo encontrás a la primera, es más rápido y 100% confiable ir al
  Método 2.

Cualquier cambio hecho así queda guardado automáticamente la próxima vez
que RetroArch escribe `retroarch.cfg` (al salir, o con "Save Current
Configuration").

## Método 2 — Editar `retroarch.cfg` directamente

Más rápido si ya sabés qué querés apagar, y funciona igual en
Android/Windows/cualquier plataforma. Abrí el archivo (ubicación típica:
`retroarch.cfg` junto al ejecutable en modo portable, o la carpeta de
config del dispositivo/app) y agregá o editá la línea con la clave exacta
(ver tabla de abajo), por ejemplo:

```
content_show_music = "false"
settings_show_achievements = "false"
quick_menu_show_cheats = "false"
```

Si la clave no existe todavía en el archivo, agregala vos mismo — RetroArch
la toma igual (los defaults de `config.def.h` solo aplican la primera vez
que una clave *no* está presente en el archivo, como ya vimos con el bug
de `assets_directory` documentado en `docs/memory.md`).

## Método 3 — Cambiar el default de tu fork (para toda instalación nueva)

Si querés que **cada APK/build que compiles** salga ya con ciertas
secciones ocultas de fábrica (sin tener que tocar el menú a mano en cada
instalación), editá los `#define DEFAULT_...` correspondientes en
`config.def.h` — están todos agrupados en dos bloques chicos y
contiguos:

- Pestañas de contenido y entradas del menú principal: `config.def.h`
  líneas ~751-900.
- `settings_show_*` y `quick_menu_show_*`: mismo rango, ~751-900.

Ejemplo — ocultar "Achievements" y "Música" por defecto en tu fork:

```diff
-#define DEFAULT_SETTINGS_SHOW_ACHIEVEMENTS true
+#define DEFAULT_SETTINGS_SHOW_ACHIEVEMENTS false
```
```diff
-#define DEFAULT_CONTENT_SHOW_MUSIC false
```
(este ya está en `false` por defecto en upstream — solo como ejemplo de
formato).

Como están todos en un bloque contiguo y chico, este diff es trivial de
volver a aplicar en cada sync con upstream (ver
`docs/retroarch-android-sync-upstream.md`) — nada que ver con la fricción
de tocar archivos activos como `menu_displaylist.c`.

**Importante**: solo afecta instalaciones **nuevas** (o después de
desinstalar y reinstalar) — igual que cualquier otro default de
`config.def.h`, no pisa un `retroarch.cfg` que ya tiene la clave escrita.

## Tabla de referencia completa

### 1. Pestañas del menú principal

| Clave en `retroarch.cfg` | Default |
|---|---|
| `content_show_settings` | `true` |
| `content_show_favorites` | `true` |
| `content_show_favorites_first` | `false` |
| `content_show_images` | `true` (si `HAVE_IMAGEVIEWER`) |
| `content_show_music` | `false` |
| `content_show_video` | `true` (si `HAVE_FFMPEG`/`HAVE_MPV`) |
| `content_show_history` | `true` |
| `content_show_playlists` | `true` |
| `content_show_playlist_tabs` | `true` |
| `content_show_explore` | `true` (si `HAVE_LIBRETRODB`) |
| `content_show_add_entry` | *(multi-valor, no on/off)* |
| `content_show_netplay` | *(multi-valor, no on/off)* |
| `content_show_contentless_cores` | *(multi-valor, no on/off)* |

Las últimas tres no son simples `true`/`false` sino listas desplegables
(ej. "mostrar como pestaña" / "mostrar dentro de Playlists" / "oculto") —
más simple tocarlas desde el menú (Método 1) que adivinar el valor
numérico del enum.

### 2. Entradas del menú principal

| Clave en `retroarch.cfg` | Default |
|---|---|
| `menu_show_load_core` | `true` |
| `menu_show_load_content` | `true` |
| `menu_show_load_disc` | `true` (si `HAVE_CDROM`) |
| `menu_show_dump_disc` | `true` (si `HAVE_CDROM`) |
| `menu_show_eject_disc` | `true` (si `HAVE_CDROM`) |
| `menu_show_information` | `true` |
| `menu_show_configurations` | `true` |
| `menu_show_overlay_settings` | `true` |
| `menu_show_latency_settings` | `true` |
| `menu_show_rewind_settings` | `true` |
| `menu_show_help` | `true` |
| `menu_show_quit_retroarch` | `true` |
| `menu_show_restart_retroarch` | `true` |
| `menu_show_reboot` | `true` |
| `menu_show_shutdown` | `true` |
| `menu_show_online_updater` | `true` |
| `menu_show_core_updater` | `true`/`false` según plataforma |
| `menu_show_core_manager_steam` | `true` |
| `menu_show_full_paths` | `true` |
| `menu_show_advanced_settings` | *(ver más abajo — caso especial)* |
| `menu_show_confirm` | `true` |
| `menu_show_load_content_animation` | según `HAVE_MENU_WIDGETS` |
| `rgui_show_start_screen` | `true` (nota: la clave real dice `rgui_` aunque el campo interno es `menu_show_start_screen`) |

### 3. Categorías completas de Settings

Todas en `true` por defecto — apagar la que no uses la hace desaparecer
por completo de la lista de Settings:

`settings_show_drivers`, `settings_show_video`, `settings_show_audio`,
`settings_show_input`, `settings_show_latency`, `settings_show_core`,
`settings_show_configuration`, `settings_show_saving`,
`settings_show_logging`, `settings_show_file_browser`,
`settings_show_frame_throttle`, `settings_show_recording`,
`settings_show_onscreen_display`, `settings_show_user_interface`,
`settings_show_ai_service`, `settings_show_accessibility`,
`settings_show_power_management`, `settings_show_achievements`,
`settings_show_network`, `settings_show_playlists`, `settings_show_user`,
`settings_show_directory`, `settings_show_steam`,
`settings_show_smb_client`.

### 4. Ítems del Quick Menu (menú in-game)

| Clave en `retroarch.cfg` | Default |
|---|---|
| `quick_menu_show_resume_content` | `true` |
| `quick_menu_show_restart_content` | `true` |
| `quick_menu_show_close_content` | `true` |
| `quick_menu_show_take_screenshot` | `true` |
| `quick_menu_show_savestate_submenu` | `true` |
| `quick_menu_show_save_load_state` | `true` |
| `quick_menu_show_replay` | `false` |
| `quick_menu_show_undo_save_load_state` | `true` |
| `quick_menu_show_add_to_favorites` | `true` |
| `quick_menu_show_add_to_playlist` | `false` |
| `quick_menu_show_start_recording` | `true` |
| `quick_menu_show_start_streaming` | `true` |
| `quick_menu_show_set_core_association` | `true` |
| `quick_menu_show_reset_core_association` | `true` |
| `quick_menu_show_options` | `true` |
| `quick_menu_show_core_options_flush` | `false` |
| `quick_menu_show_controls` | `true` |
| `quick_menu_show_cheats` | `true` |
| `quick_menu_show_shaders` | `true` |
| `quick_menu_show_save_core_overrides` | `true` |
| `quick_menu_show_save_game_overrides` | `true` |
| `quick_menu_show_save_content_dir_overrides` | `true` |
| `quick_menu_show_information` | `true` |
| `quick_menu_show_download_thumbnails` | `true` |
| `quick_menu_show_game_ai` | (si `HAVE_GAME_AI`) |

**Nota**: `quick_menu_show_recording` y `quick_menu_show_streaming`
existen como campos declarados en `configuration.h` pero no están
registrados en ningún lado ni se leen en el código del menú — son
vestigios sin efecto actual, no pierdas tiempo con ellos.

## Caso especial: "Show Advanced Settings"

`menu_show_advanced_settings` no oculta una sección puntual — es el
interruptor general que decide si se muestran o no las opciones que el
propio código marca como "avanzadas" dentro de cada pantalla de Settings.
Es la herramienta a usar para el problema de "hay demasiadas opciones
sueltas dentro de un submenú", cuando ninguna de las 76 claves de arriba
aplica (porque esas son a nivel de sección/pestaña/ítem, no de opción
individual suelta).
