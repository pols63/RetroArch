# Cómo probar: instalación masiva y backup de cores (Android, SAF)

Guía práctica para compilar y probar en dispositivo la implementación descrita
en `docs/retroarch-android-bulk-cores.md`.

**Estado: verificado en dispositivo real.** El flujo completo — bulk
install, bulk backup, y la extensión de `info.zip` (offline core-info
database, ver `docs/memory.md`) — se probó de punta a punta y el usuario lo
confirmó funcionando. Esta guía ya no es especulativa; las secciones 3 y 4
reflejan lo que efectivamente se verificó, no una lista de riesgos sin
probar. Para el detalle de los bugs encontrados y corregidos en el camino
(un crash de use-after-free reproducible al 100%, diagnosticado con
`adb logcat -b crash` + `llvm-addr2line` contra el `.so` sin stripear), ver
`docs/memory.md` — esta guía se queda solo con los pasos prácticos.

## 1. Prerrequisitos

- **Android SDK** con `compileSdk 36` / `targetSdk 36` instalado (vía Android
  Studio o `sdkmanager`).
- **NDK exacto**: `29.0.14206865` (fijado en
  `pkg/android/phoenix/build.gradle:37`). Instalarlo con Android Studio (SDK
  Manager → SDK Tools → NDK, versión específica) o:
  ```bash
  sdkmanager "ndk;29.0.14206865"
  ```
- JDK 17+ (lo que pida la versión de Android Studio actual).
- Un dispositivo Android real con **depuración USB** habilitada (preferible
  a un emulador, porque el flujo de SAF/`ACTION_OPEN_DOCUMENT_TREE` es más
  confiable de probar con el picker real de archivos del dispositivo).

`HAVE_SAF` ya está fijo en `1` en
`pkg/android/phoenix-common/jni/Android.mk:13`, así que no hace falta
activar ningún flag extra — el código nuevo se compila siempre en esta
variante.

## 2. Compilar e instalar

`aarch64` es la variante a usar para probar en un teléfono moderno de
64-bit. Evitar `playStoreNormal`/`playStorePlus` para este test, ya que
agregan la capa de Play Feature Delivery, que no hace falta acá.

El primer build va a tardar bastante (compila `griffin.c`, el archivo
unity-build gigante donde se agregaron las dos tasks nuevas). Si falla la
compilación, el error va a indicar el archivo y la línea exactos — los
candidatos más probables son:

- `tasks/task_core_bulk_install.c`
- `tasks/task_core_bulk_backup.c`
- `menu/menu_displaylist.c`
- `menu/cbs/menu_cbs_ok.c`

### Opción A: Android Studio (recomendado)

Más simple si no tenés el entorno Android configurado a mano — Android
Studio resuelve el SDK/NDK sin configuración manual de variables de
entorno.

1. Instalar Android Studio (https://developer.android.com/studio) si no lo
   tenés.
2. `File → Open` y seleccionar la carpeta `pkg/android/phoenix/` (esa
   carpeta específica, **no** la raíz del repo — es el proyecto Gradle de
   Android).
3. Android Studio va a detectar que falta el NDK `29.0.14206865` y va a
   ofrecer instalarlo. Si no lo hace solo: `File → Settings → Languages &
   Frameworks → Android SDK → SDK Tools`, tildar "Show Package Details" y
   marcar esa versión exacta del NDK.
4. Conectar el celular por USB con depuración USB habilitada
   (`Configuración → Opciones de desarrollador → Depuración USB`; si no
   aparece "Opciones de desarrollador", tocar 7 veces "Número de
   compilación" en `Configuración → Acerca del teléfono`).
5. Arriba a la derecha, en el selector de variante de build, elegir
   **`aarch64Debug`** (`Build → Select Build Variant...` si no se ve el
   selector).
6. Botón ▶️ Run (o `Shift+F10`) — compila e instala directo en el celular
   conectado.

El logcat se puede ver directo en la pestaña "Logcat" de Android Studio en
lugar de la terminal (ver paso 4 más abajo para el filtro a usar).

### Opción B: línea de comandos

Solo si preferís no usar la GUI. Necesita `ANDROID_HOME` (o
`ANDROID_SDK_ROOT`) apuntando al SDK y el NDK `29.0.14206865` ya instalado
por separado.

Desde `pkg/android/phoenix/`:

```bash
./gradlew assembleAarch64Debug
```

El APK queda en `pkg/android/phoenix/build/outputs/apk/aarch64/debug/`.
Instalar con:

```bash
adb install -r pkg/android/phoenix/build/outputs/apk/aarch64/debug/phoenix-aarch64-debug.apk
```

## 3. Probar el flujo manualmente

1. Abrir RetroArch → **Main Menu → Manage Cores**. Deben aparecer dos
   entradas nuevas: **"Install Cores from Folder (Bulk)"** y **"Backup
   Cores"**, justo después de "Install or Restore a Core".

2. **Bulk install**:
   - Copiar algunos cores (`*_libretro_android.so`) a una carpeta cualquiera
     del dispositivo (por ejemplo, Descargas).
   - Entrar a "Install Cores from Folder (Bulk)", elegir esa carpeta en el
     picker nativo de Android.
   - Verificar que aparezca la pantalla de confirmación con la lista de
     archivos encontrados (marcando cuáles sobrescriben un core ya
     instalado) y los botones "Install All" / "Cancel".
   - Confirmar y verificar que:
     - Aparezca el progreso de la task ("Installing cores...").
     - Al final salga el toast de resumen ("Bulk core install: X installed,
       Y failed").
     - Los cores realmente queden instalados (probar cargarlos).

3. **Backup**:
   - Entrar a "Backup Cores", elegir una carpeta destino.
   - Confirmar la lista (con tamaño total aproximado).
   - Verificar con un explorador de archivos que los `.so` aparezcan
     copiados en esa carpeta.
   - Si `path_libretro_info` tiene archivos `.info` instalados (ver punto
     5), debería aparecer también un `info.zip` junto a los `.so` — un zip
     STORED (sin comprimir) válido, abrible con cualquier descompresor.

4. Revisar logs con logcat filtrando por los tags agregados:
   ```bash
   adb logcat | grep -E "Core Bulk Install|Core Bulk Backup"
   ```
   Debería haber una línea por archivo procesado, con su resultado.

5. **Offline core-info database (info.zip)**: ver `docs/memory.md` para el
   detalle completo. Resumen del ciclo:
   - Colocar (o dejar que "Backup Cores" genere, punto 3) un `info.zip` en
     la misma carpeta SAF que se usa para "Install Cores from Folder
     (Bulk)".
   - La pantalla de confirmación debe mostrar la fila "info.zip found -
     core info database will be updated" (además de la lista de cores, o
     sola si la carpeta no tiene cores, solo `info.zip`).
   - Tras "Install All", el toast final debe incluir "; core info database
     updated", y "Manage Cores" debe mostrar nombre real y licencia de cada
     core (no "License: N/A") sin ningún paso manual adicional.

## 4. Puntos verificados / a vigilar

Ya verificado en dispositivo real (Android 13, `aarch64Debug`):

- La pantalla de confirmación lista los archivos correctamente y no rompe
  el renderizado del driver de menú en uso.
- El toast de resumen final aparece con el conteo correcto, tanto para
  bulk install como para bulk backup.
- El ciclo completo de `info.zip` (backup → instalación offline → Manage
  Cores con nombre/licencia correctos) funciona de punta a punta.
- La app no crashea durante una instalación masiva de varios cores
  seguidos (bug de use-after-free encontrado y corregido — ver
  `docs/memory.md`).
- Repetir "Install Cores from Folder (Bulk)" sobre una carpeta cuyos cores
  ya están instalados no se cuelga ni crashea (bug de reapertura de
  archivo en cada tick al calcular el CRC del core existente, corregido —
  ver `docs/memory.md`; de paso, el reinstall corre notablemente más
  rápido que antes del fix).
- El progreso de "Install Cores from Folder (Bulk)" se ve como una única
  barra avanzando ("Installing cores... (i/N) nombre.so"), sin mensajes
  superpuestos de cada archivo individual (fix de `mute` en
  `task_push_core_restore()` — ver `docs/memory.md`).

Pendiente de verificar / puntos de riesgo restantes:

- El comportamiento con una carpeta vacía o sin cores ni info.zip: debería
  mostrar el toast "No core files found in that folder." (instalación) o
  "No installed cores found to back up." (backup) en vez de abrir una
  pantalla vacía.
- El detalle de fallidos en el resumen final cuando hay archivos rechazados
  (nombre de core bloqueado, archivo inválido, etc.).
- Los otros drivers de menú además del probado (XMB/Ozone/MaterialUI —
  confirmar en cuál se probó y extender a los demás si hace falta).

## 5. Si algo falla

Guardar el error de compilación completo (o el log de logcat si falla en
runtime) para poder ubicar la causa exacta contra el archivo/línea
correspondiente.

## 6. Problemas comunes de entorno (no relacionados con el código)

### `[CXX1429] ... ERROR: NDK path cannot contain spaces`

El sistema de build nativo de este proyecto (`ndk-build`, basado en GNU
Make, ver `pkg/android/phoenix-common/jni/Android.mk`) tiene una limitación
histórica: no tolera espacios en la ruta del SDK/NDK. Si tu usuario de
Windows tiene espacio en el nombre (por ejemplo `C:\Users\Jean
Paul\...`), el SDK por defecto queda instalado bajo esa ruta y el build
falla con este error — independientemente del código que se esté
compilando.

**Solución**: mover el SDK a una ruta sin espacios.

1. En Android Studio: `File → Settings → Languages & Frameworks → Android
   SDK`.
2. Cambiar el campo **"Android SDK Location"** a una ruta sin espacios, por
   ejemplo `D:\Android\Sdk`.
3. Aplicar. Android Studio va a ofrecer mover el contenido existente o
   descargarlo de nuevo en la nueva ubicación — descargar de nuevo es más
   seguro (evita problemas al copiar symlinks/permisos a medias).
4. En **SDK Tools**, confirmar que el NDK `29.0.14206865` sigue instalado
   (tildarlo de nuevo si hizo falta reinstalar el SDK).
5. `File → Sync Project with Gradle Files` y volver a compilar.

### Compilar desde terminal (sin Android Studio abierto): falta `JAVA_HOME`

Si se corre `./gradlew` directo desde una terminal que no heredó el entorno
de Android Studio, falla con `ERROR: JAVA_HOME is not set and no 'java'
command could be found in your PATH`. Android Studio trae su propio JDK
embebido (JBR) que sirve perfectamente para esto — no hace falta instalar
uno aparte:

```bash
JAVA_HOME="/c/Program Files/Android/Android Studio/jbr" ./gradlew assembleAarch64Debug
```

(ajustar la ruta si Android Studio está instalado en otro lado).

### `adb shell run-as <paquete> sh -c "..."` da resultados sin sentido

Con `adb.exe` en Windows, invocado desde una terminal tipo Git Bash/MSYS,
pasar un comando compuesto (`&&`, `|`, `*`) dentro de `sh -c "..."` es poco
fiable: `adb shell` reconcatena todos los argumentos con espacios antes de
enviarlos al dispositivo, así que las comillas que protegían esos
metacaracteres en el comando local se pierden, y el shell remoto los
interpreta sueltos — sin dar un error claro, solo resultados que no
corresponden a lo que se pidió (por ejemplo, listar el directorio raíz de
datos de la app en vez del subdirectorio pedido).

**Evitarlo**: para cualquier operación remota con metacaracteres, usar
varios comandos `adb shell run-as <paquete> <cmd> <args...>` simples, sin
`sh -c`, sin `|`, sin `&&`, sin glob remoto (expandir el glob del lado
local/Bash y pasar la lista de nombres ya expandida como argumentos
separados). Esto sirvió, por ejemplo, para copiar en bloque un directorio
de archivos `.info` a la carpeta privada de la app vía `run-as` (necesario
porque `run-as` solo funciona en builds *debuggable*, como esta — no en un
build de release firmado sin dispositivo rooteado).
