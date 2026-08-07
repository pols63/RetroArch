# Cómo probar: instalación masiva y backup de cores (Android, SAF)

Guía práctica para compilar y probar en dispositivo la implementación descrita
en `docs/retroarch-android-bulk-cores.md`. Nada de este código pudo
compilarse ni probarse durante la implementación (no hay NDK/Gradle en ese
entorno), así que este es el primer punto de validación real.

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

4. Revisar logs con logcat filtrando por los tags agregados:
   ```bash
   adb logcat | grep -E "Core Bulk Install|Core Bulk Backup"
   ```
   Debería haber una línea por archivo procesado, con su resultado.

## 4. Puntos a vigilar de cerca

Sin haber podido compilar ni probar visualmente, estos son los puntos de
mayor riesgo a revisar primero:

- Que la pantalla de confirmación efectivamente liste los archivos y no
  rompa el renderizado del driver de menú en uso (XMB/Ozone/MaterialUI) — se
  usó el mecanismo genérico de listas de RetroArch, pero no se verificó
  visualmente en ningún driver.
- El comportamiento con una carpeta vacía o sin cores: debería mostrar el
  toast "No core files found in that folder." (instalación) o "No installed
  cores found to back up." (backup) en vez de abrir una pantalla vacía.
- Que cancelar y volver a intentar (un segundo scan) no deje estado colgado
  entre operaciones.
- El detalle de fallidos en el resumen final cuando hay archivos rechazados
  (nombre de core bloqueado, archivo inválido, etc.).

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
