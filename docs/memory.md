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

## Crash al pulsar "Install All" (bulk install) y su fix

Al probar por primera vez el flujo real en dispositivo (sección "Próximos
pasos" de más abajo), "Install All" cerraba la app de forma consistente,
dejando solo 1-2 cores instalados de la carpeta. Diagnosticado con
`adb logcat -d -b crash` (dispositivo conectado, `adb` en
`D:\Android\Sdk\platform-tools`) y símbolos sin recortar en
`pkg/android/phoenix/build/intermediates/cxx/Debug/<hash>/obj/local/arm64-v8a/libretroarch-activity.so`
resueltos con `llvm-addr2line.exe` del NDK (el `.so` empaquetado en el APK
va stripped, ese intermedio no).

**Causa raíz encontrada (tras dos hipótesis descartadas por el propio
logcat — ver más abajo "Hipótesis descartadas"):** bug preexistente en
`tasks/task_core_backup.c`, `task_core_backup_finder()` — la función que
`task_push_core_restore()`/`task_push_core_backup()` usan internamente
para comprobar "¿ya hay una tarea de backup/restore para este core en
curso?" (evita duplicados concurrentes). Esa función recorre las listas
internas del task queue y hace `task->state` sin comprobar antes si la
tarea ya terminó. Pero tanto `task_core_backup_handler()` como
`task_core_restore_handler()` **liberan su `task->state` en el mismo
instante en que se marcan `FINISHED`** (ver su etiqueta `task_finished`),
mucho antes de que el `retro_task_t` en sí sea retirado/liberado de las
colas. Hay por tanto una ventana en la que una tarea sigue "visible" en
`tasks_running`/`tasks_finished` pero su `state` ya es un puntero
colgante — si algo la encuentra y toca `task->state` en ese hueco, UAF.

Nadie lo había disparado antes porque en el uso normal (un restore desde
el menú, a mano) pasan muchos frames entre una restauración y la
siguiente. El instalador masivo encadena restores sin pausa, así que
dispara la ventana en *todas* las iteraciones — de ahí que el crash fuera
100% reproducible y con la *misma dirección de fallo exacta*
(`0x6c616e7200000000`, restos de un string reutilizando el bloque recién
liberado) en cada intento, incluso tras builds distintos: no era una
carrera de temporización con resultado variable, sino un UAF secuencial
determinista en el mismo hilo worker.

**Fix aplicado** (`tasks/task_core_backup.c`,
`task_core_backup_finder()`): comprobar `task_get_flags(task) &
RETRO_TASK_FLG_FINISHED` y devolver `false` *antes* de tocar
`task->state`. Una tarea ya terminada no está "en curso" de todos modos,
que es lo único que esta función responde.

### Hipótesis descartadas por el camino (documentadas para no repetirlas)

1. **Puntero de tarea retenido entre ticks** (`h->current_subtask` en
   `tasks/task_core_bulk_install.c`, sondeado con `task_get_flags()` desde
   el propio handler del coordinador bulk): real problema de diseño (el
   `retro_task_t` puede liberarse por completo desde el hilo principal en
   cualquier momento tras terminar, sin que el hilo worker que lo sondea
   se entere), pero no era la causa de *este* crash concreto. Se
   sustituyó por sondear con `task_queue_find()` (thread-safe, nunca
   devuelve un puntero) vía la nueva `task_core_backup_find()` — cambio
   correcto y ya aplicado, se mantiene.
2. **`core_info_find()` sin lock, llamado desde el hilo worker mientras
   el hilo principal reconstruye la lista global de cores** (vía
   `CMD_EVENT_CORE_INFO_INIT`, disparado por el callback de fin de cada
   restore): también un riesgo real y ya documentado en el propio código
   base (`task_core_updater.c` lo evita a propósito con
   `validate_path=false`), y se corrigió igualmente añadiendo un
   parámetro `core_display_name` a `task_push_core_restore()` para que el
   bulk-install lo evite. Cambio correcto y se mantiene, pero el logcat
   demostró que *tampoco* era la causa de este crash (misma dirección de
   fallo exacta antes y después de aplicarlo).

Las tres correcciones son independientes y las tres se quedan (cada una
cierra un hueco de concurrencia real), pero la que realmente paraba el
crash reproducido era la tercera (`task_core_backup_finder`).

**Estado**: **confirmado en dispositivo — "Install All" ya no cierra la
app** y los 5 cores de la carpeta de prueba se instalaron completos (antes
solo llegaban 1-2). Pendiente: decidir si commitear las tres correcciones
(quedan como diffs sin commitear en `tasks/task_core_backup.c`,
`tasks/task_core_bulk_install.c`, `tasks/tasks_internal.h`,
`menu/cbs/menu_cbs_ok.c`, `samples/tasks/core_backup/core_backup_io_test.c`).

## "License: N/A" / nombre de core no identificado tras instalar

Tras el fix del crash, los cores instalados (bulk o individual, da igual)
aparecían en "Manage Cores" con el nombre de archivo crudo y
"License: N/A". **No es un bug** — es que la carpeta `info` de la app
(`libretro_info_path` en `retroarch.cfg`, en este dispositivo
`/data/user/0/com.retroarch.aarch64/info`) solo tenía `core_info.cache`,
sin ningún `.info` real. Esta build de Android nunca empaquetó la base de
datos `libretro-core-info` en el APK — `fetch-submodules.sh` solo trae
`media/shaders_cg`, `media/overlays`, `media/assets`, `media/autoconfig`,
`media/libretrodb` (sin core-info), y `build.gradle` solo declara
`assets.srcDirs = ['assets', '../../../media/assets']`. RetroArch espera
que el usuario los traiga con Main Menu → Online Updater → Update Core
Info Files (descarga `https://buildbot.libretro.com/assets/frontend/info.zip`,
un simple zip plano de un `.info` por core, y lo extrae en
`libretro_info_path`).

El usuario pidió expresamente que esto se pueda hacer **offline** (él
mismo trae el archivo y lo copia al dispositivo) en vez de bundlearlo en
el proyecto o depender de red en el propio dispositivo. Confirmado que
ese flujo ya funciona tal cual, sin ningún cambio de código: el
mecanismo es puramente "lee lo que haya en la carpeta configurada", así
que basta con colocar los `.info` ahí por el medio que sea.

Procedimiento offline verificado esta sesión (con el mismo
`info.zip` de arriba, descargado UNA vez en cualquier PC con internet):
1. Descargar y descomprimir `info.zip` (carpeta plana, un `.info` por
   core, sin procesamiento).
2. Copiar los `.info` a la carpeta que apunte `libretro_info_path` (ver
   `retroarch.cfg` del dispositivo, o cambiar el ajuste "Core Info
   Directory" en Settings → Directory a una carpeta externa/SD que el
   usuario pueda escribir con un explorador de archivos normal, sin
   necesitar `adb`/`run-as` cada vez — recomendado para uso futuro sin mí
   de por medio).
3. Si la carpeta de destino es la privada de la app (como en este
   dispositivo de prueba), hace falta `adb push` a una ruta temporal +
   `adb shell run-as com.retroarch.aarch64 cp ... ` (no se puede escribir
   ahí directo por USB). Borrar `core_info.cache` y reiniciar la app para
   forzar el rescan (el escaneo de `info` no es instantáneo/reactivo, solo
   ocurre al boot o al forzar `CMD_EVENT_CORE_INFO_INIT`).

**Gotcha de entorno encontrado en el camino**: en este Windows con
`adb.exe`, pasar `run-as <pkg> sh -c "..."` desde Git Bash es poco fiable
— `adb shell` reconcatena los argumentos con espacios antes de mandarlos
al dispositivo, así que las comillas que protegen `&&`, `|`, `*`, etc. en
el string interno se pierden y el shell remoto los interpreta sueltos
(dan resultados confusos y silenciosos, no errores claros). Evitarlo:
para cualquier operación con metacaracteres, mejor varios comandos
`adb shell run-as <pkg> <cmd> <args...>` simples sin `sh -c`, sin `|`,
sin `&&`, sin glob remoto (expandir el glob en el lado local/Bash y pasar
la lista de nombres ya expandida).

## Próximos pasos (en orden)

1. Decidir si commitear los cambios de la sección "Trabajo hecho en esta
   sesión: menú sin iconos" (assets bundling + fix de `assets_directory`)
   — están verificados funcionando pero no forman parte de la feature de
   bulk-cores en sí.
3. Con el crash resuelto, probar el flujo manual completo de
   bulk-install/backup (ver `docs/retroarch-android-bulk-cores-testing.md`,
   sección 3): verificar instalación/backup real de archivos + toast de
   resumen + logs (`adb logcat | grep -E "Core Bulk Install|Core Bulk
   Backup"`).
4. Puntos de riesgo a vigilar especialmente al probar: que la pantalla de
   confirmación renderice bien en el driver activo (XMB/Ozone/MaterialUI),
   comportamiento con carpeta vacía, y que cancelar/reintentar no deje
   estado colgado.
5. El detalle de fallidos en el resumen final cuando hay archivos
   rechazados (nombre de core bloqueado, archivo inválido, etc.) — todavía
   sin probar.
6. Backup masivo (`task_core_bulk_backup.c`) no tiene el patrón de
   subtarea que causó el crash de install (copia los archivos
   directamente, sin pushear tareas hijas) — no debería estar afecto, pero
   tampoco se ha probado en dispositivo todavía.
