# Memoria de sesión: instalación masiva y backup de cores (Android, SAF)

Contexto para retomar esta tarea en otra sesión. Rama `dev-masscores`.
Estado del código de la feature de bulk-cores: **implementación completa y
committeada** (commit `9633d4977d`, mensaje "dev"). El **build ya compila e
instala correctamente** en dispositivo (Opción A, Android Studio,
`aarch64Debug`) — el problema de NDK/espacios en la ruta del SDK descrito
más abajo se resolvió. Lo que falta es **probar el flujo manual de
bulk-install/backup en sí** (sección 3 de
`docs/retroarch-android-bulk-cores-testing.md`), que todavía no se hizo esta
sesión porque primero hubo que resolver un problema no relacionado: el menú
se veía sin iconos y pixelado en el dispositivo de prueba, lo cual bloqueaba
poder ver/usar cualquier pantalla nueva del menú con comodidad. Ver
"Trabajo hecho en esta sesión" más abajo.

## Qué se pidió

Dos funcionalidades nuevas para el fork Android de RetroArch, descritas en
`docs/retroarch-android-bulk-cores.md`:

1. Instalar en un solo paso todos los cores `.so` de una carpeta elegida
   por el usuario vía SAF (Storage Access Framework).
2. Backup de los cores instalados hacia una carpeta elegida por el usuario
   vía SAF.

Restricciones no negociables del doc: no tocar la carpeta privada de
cores, no pedir `MANAGE_EXTERNAL_STORAGE`, reutilizar la lógica de
instalación existente (no reimplementarla), siempre con confirmación
explícita (un solo diálogo por lote) y resumen final.

## Qué se implementó

Investigación previa reveló que el fork ya tenía un puente SAF
nativo↔Java completo (`android_show_saf_tree_picker()` /
`safTreeAdded`) y un VFS que ya lee/escribe rutas `saf://`
transparentemente — por eso todo se implementó en **C nativo**, reutilizando
ese puente, en vez del enfoque Kotlin/`DocumentFile` que proponía el doc
original (escrito sin conocer esa infraestructura).

Archivos nuevos:
- `tasks/task_core_bulk_install.c` — escanea la carpeta SAF elegida y por
  cada archivo llama a `task_push_core_restore()` (la función real de
  "instalar un core desde un path", ya usada por "Install or Restore a
  Core"), sin duplicar lógica de instalación.
- `tasks/task_core_bulk_backup.c` — copia cada core de la carpeta privada
  al árbol SAF de destino, con el mismo patrón de copia por chunks que ya
  usa el restore de `task_core_backup.c`.

Archivos modificados (lista completa en el commit `9633d4977d`):
`tasks/task_core_backup.c` (agregado parámetro opcional `out_task` a
`task_push_core_restore`, sin romper los 3 call sites existentes),
`tasks/tasks_internal.h`, `frontend/drivers/platform_unix.c/.h` (SAF con
"propósito": Browse/Bulk Install/Backup), `menu/menu_displaylist.c/.h`,
`menu/cbs/menu_cbs_ok.c`, `menu/cbs/menu_cbs_deferred_push.c`,
`menu/cbs/menu_cbs_sublabel.c`, `menu/menu_cbs.h`, `msg_hash.h`,
`msg_hash_lbl_str.h`, `intl/msg_hash_lbl.h`, `intl/msg_hash_us.h`,
`griffin/griffin.c` (registro de los 2 archivos nuevos en el build
unity de Android), `samples/tasks/core_backup/core_backup_io_test.c`
(actualizado por el cambio de firma de `task_push_core_restore`).

Dos entradas nuevas en el menú "Manage Cores": **"Install Cores from
Folder (Bulk)"** y **"Backup Cores"**, cada una con su pantalla de
confirmación (lista de archivos + "Install All"/"Backup All" + "Cancel"),
logging por archivo vía `RARCH_LOG`, resumen final vía el sistema de
notificaciones nativo de RetroArch.

Se verificó el checklist completo de la Fase 3 del doc original y se hizo
una revisión cruzada de todos los identificadores nuevos en todo el repo
(sin poder compilar, por falta de NDK en el entorno donde se escribió el
código).

Scope cuts deliberados (documentados, no bloqueantes): no se agregaron
íconos por-driver (xmb/ozone/materialui) para las entradas nuevas —
cosmético, usan ícono por defecto.

## Documentación relacionada

- `docs/retroarch-android-bulk-cores.md` — spec original (en español).
- `docs/retroarch-android-bulk-cores-testing.md` — guía de compilación y
  prueba en dispositivo, con troubleshooting. Incluye pasos para Android
  Studio (recomendado) y línea de comandos.

## Estado actual del intento de build (dónde se quedó)

Entorno: Windows, Android Studio, dispositivo físico conectado
`motorola moto g(20)`, variante de build **`aarch64Debug`** (evitar
`playStore*`, que agregan Play Feature Delivery innecesaria para este
test).

**Problema encontrado y resuelto:** el NDK (`ndk-build`, basado en GNU
Make) no tolera espacios en la ruta del SDK. El usuario de Windows es
`C:\Users\Jean Paul\...` (espacio en "Jean Paul"), lo que rompía el build
con `[CXX1429] ... ERROR: NDK path cannot contain spaces`. Esto es un
problema de entorno, no del código.

Pasos ya dados para resolverlo:
1. Se copió la carpeta completa del SDK de
   `C:\Users\Jean Paul\AppData\Local\Android\Sdk` a `D:\Android\Sdk`
   (sin espacios).
2. Se cambió el default de Android Studio a `D:\Android\Sdk` (`File →
   Settings → Languages & Frameworks → Android SDK`).
3. **Importante, aprendido en el camino**: ese setting global NO alcanza
   para un proyecto que ya tiene su propio `pkg/android/phoenix/local.properties`
   con `sdk.dir` apuntando a la ruta vieja — hay que cambiarlo puntualmente
   vía `File → Project Structure → SDK Location`.
4. Al hacerlo, Android Studio mostró el diálogo "Android SDK Manager: The
   project and Android Studio point to different Android SDKs" — el
   usuario apretó **"Use Android Studio's SDK"**, que reescribe
   `local.properties` para apuntar a `D:\Android\Sdk`.

**Último paso pendiente de confirmar en la próxima sesión**: si tras
aceptar ese diálogo y sincronizar, el build (`▶️ Run` con variante
`aarch64Debug`) compila correctamente. No se confirmó todavía si el NDK
`29.0.14206865` quedó reconocido en la carpeta copiada
(`D:\Android\Sdk\ndk\29.0.14206865`) — si Android Studio no lo detecta ahí,
puede hacer falta reinstalarlo desde SDK Tools apuntando ya a la ubicación
nueva.

## Trabajo hecho en esta sesión: menú sin iconos (Android)

No relacionado directamente con la feature de bulk-cores, pero bloqueaba
poder probarla cómodamente: tras compilar e instalar por primera vez, el
menú se veía con fuente bitmap pixelada y sin iconos (XMB/Ozone/MaterialUI
degradados). Causa raíz identificada y arreglada:

1. **Assets no bundleados**: los assets del menú (`libretro/retroarch-assets`
   — iconos, TTF, wallpapers) no viajaban con el APK. Fix: se agregó
   `media/assets` (la ruta donde `fetch-submodules.sh` clona ese repo) como
   `sourceDirs` adicional de Gradle en
   `pkg/android/phoenix/build.gradle:136` (`assets.srcDirs = ['assets',
   '../../../media/assets']`), para que Gradle empaquete ese contenido
   dentro del APK en cada build. Requiere que `media/assets` esté poblado
   localmente (clonar `libretro/retroarch-assets` ahí, o correr
   `fetch-submodules.sh`) — **no se commitea** (gitignored a propósito, es
   contenido binario pesado, igual que en upstream).
2. **Bug real de RetroArch (Android) en la ruta de extracción**: aun con los
   assets bien bundleados en el APK, seguían sin aparecer. Se verificó con
   `adb run-as ... ls` que el extractor nativo
   (`tasks/task_decompress.c:file_decompressed_subdir`) le quita el prefijo
   `"assets/"` a cada entrada del APK antes de escribirla, así que el
   contenido queda plano bajo el dataDir de la app (`.../xmb`, `.../ozone`,
   etc.), **no** bajo `.../assets/`. Pero el default de
   `assets_directory` en `frontend/drivers/platform_unix.c:1852` sí le
   agregaba ese `/assets` extra — apuntaba una carpeta más abajo de donde
   realmente caían los archivos. Coincide con un `TODO/FIXME` ya presente en
   ese mismo bloque de código ("change the extraction method so it honors
   the user defined paths instead"). Fix: se cambió ese default para que
   apunte directo a `app_dir` (sin agregar `"assets"`), ver el diff de
   `platform_unix.c`.
3. Importante para pruebas futuras: un `retroarch.cfg` ya existente en el
   dispositivo **no** recoge un default corregido — el key persistido gana
   sobre el default de código. Hace falta una desinstalación completa
   (no solo reinstalar/actualizar) para que se regenere el config y se vea
   el fix.
4. **Ambos cambios están hechos pero sin commitear todavía**
   (`pkg/android/phoenix/build.gradle` y `frontend/drivers/platform_unix.c`
   aparecen como `M` en `git status`). Confirmado funcionando en dispositivo
   (iconos cargan correctamente) — falta decidir si commitear.

## Próximos pasos (en orden)

1. Decidir si commitear los dos cambios de la sección anterior (assets
   bundling + fix de `assets_directory`) — están verificados funcionando
   pero no forman parte de la feature de bulk-cores en sí.
2. Probar el flujo manual completo de bulk-install/backup (ver
   `docs/retroarch-android-bulk-cores-testing.md`, sección 3):
   Manage Cores → las dos entradas nuevas → confirmar → verificar
   instalación/backup real de archivos + toast de resumen + logs
   (`adb logcat | grep -E "Core Bulk Install|Core Bulk Backup"`). Ahora que
   el menú renderiza con iconos, ya se puede evaluar visualmente si la
   pantalla de confirmación nueva se ve bien en el driver activo.
3. Puntos de riesgo a vigilar especialmente al probar (sin verificar
   visualmente en ningún driver de menú todavía): que la pantalla de
   confirmación renderice bien en el driver activo (XMB/Ozone/MaterialUI),
   comportamiento con carpeta vacía, y que cancelar/reintentar no deje
   estado colgado.
4. El detalle de fallidos en el resumen final cuando hay archivos
   rechazados (nombre de core bloqueado, archivo inválido, etc.) — todavía
   sin probar.
