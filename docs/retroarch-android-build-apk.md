# Cómo abrir el proyecto Android y generar el APK

Guía práctica para abrir este fork en Android Studio y generar un APK
instalable (de prueba o "distribuible" para compartir). Para el flujo de
compilación e instalación específico de la feature de bulk cores, ver
`docs/retroarch-android-bulk-cores-testing.md` — esta guía es la versión
general, para cualquier build.

## 1. Prerrequisitos

- **Android Studio** (https://developer.android.com/studio) — trae su
  propio JDK embebido (JBR), así que no hace falta instalar un JDK aparte
  si vas a compilar desde la IDE.
- **Android SDK** con `compileSdk`/`targetSdk 36` (Android Studio lo
  resuelve solo al abrir el proyecto).
- **NDK exacto**: `29.0.14206865`, fijado en
  `pkg/android/phoenix/build.gradle:37`. Si no lo tenés, Android Studio te
  va a ofrecer instalarlo al abrir el proyecto (ver paso 3 más abajo).
- La ruta donde vive el SDK **no debe tener espacios** (ver la sección de
  problemas comunes al final) — por ejemplo `D:\Android\Sdk`, no
  `C:\Users\Jean Paul\AppData\...`.
- Para instalar y probar en un dispositivo real: depuración USB habilitada
  (`Configuración → Opciones de desarrollador → Depuración USB`; si no
  aparece "Opciones de desarrollador", tocar 7 veces "Número de
  compilación" en `Configuración → Acerca del teléfono`).

## 2. Abrir el proyecto en Android Studio

1. `File → Open`.
2. Seleccionar la carpeta **`pkg/android/phoenix/`** — esa carpeta
   puntual, **no** la raíz del repositorio. Es la que contiene el proyecto
   Gradle de Android (`build.gradle`, `settings.gradle`); el resto del
   repo es el código nativo compartido con las demás plataformas, que
   Gradle referencia desde ahí afuera.
3. Esperar a que termine el sync de Gradle (barra de progreso abajo). La
   primera vez puede tardar bastante porque descarga dependencias.
4. Si falta el NDK `29.0.14206865`, Android Studio muestra un aviso
   arriba del editor ofreciendo instalarlo — aceptarlo. Si no aparece
   solo: `File → Settings → Languages & Frameworks → Android SDK → SDK
   Tools`, tildar "Show Package Details" y marcar esa versión exacta.
5. Volver a sincronizar (`File → Sync Project with Gradle Files`) si hizo
   falta instalar algo.

## 3. Elegir la variante de build

Arriba a la izquierda de la ventana principal (o `Build → Select Build
Variant...`) hay un selector de variante por módulo. Este proyecto define
varias combinaciones (ver `productFlavors` en `build.gradle`):

| Flavor | Para qué sirve |
|---|---|
| **`aarch64`** | La recomendada para probar en un teléfono moderno de 64-bit. Compila solo `arm64-v8a`/`x86_64`. |
| `normal` | Todas las ABIs, sin filtrar — build más pesado y lento, normalmente innecesario. |
| `ra32` | Solo ABIs de 32-bit (`armeabi-v7a`/`x86`) — dispositivos viejos. |
| `playStoreNormal` / `playStorePlus` | Variantes con Play Feature Delivery (descarga de cores on-demand vía Play Store). Evitarlas salvo que estés probando específicamente eso — agregan complejidad que no hace falta para uso normal. |

Combinado con el **build type** (`debug` o `release`), el selector muestra
opciones como `aarch64Debug` o `aarch64Release`. Elegí **`aarch64Debug`**
para desarrollo/pruebas normales.

## 4. Generar un APK para probar (debug)

Es el camino más simple y el que conviene usar mientras estás
desarrollando o probando algo puntual.

### Opción A: Run directo a un dispositivo conectado

1. Conectar el celular por USB (con depuración USB habilitada) o tener un
   emulador corriendo.
2. Con la variante `aarch64Debug` seleccionada, tocar el botón ▶️ Run (o
   `Shift+F10`).

Compila e instala directo en el dispositivo/emulador — no genera un
archivo `.apk` que puedas compartir, es solo para probar en el momento.

### Opción B: Generar el archivo `.apk`

`Build → Build Bundle(s) / APK(s) → Build APK(s)`. Al terminar aparece un
aviso abajo a la derecha ("APK(s) generated successfully") con un enlace
**"locate"** que abre la carpeta que lo contiene.

También por línea de comandos, desde `pkg/android/phoenix/`:

```bash
./gradlew assembleAarch64Debug
```

El APK queda en:

```
pkg/android/phoenix/build/outputs/apk/aarch64/debug/phoenix-aarch64-debug.apk
```

## 5. Generar el APK distribuible (release)

Un APK "release" está optimizado (minificado/optimizado según
configuración) y pensado para compartir o instalar fuera de tu propio
dispositivo de desarrollo. Hay dos caminos, según qué tan formal necesites
que sea la firma:

### Opción A — Rápida: firma de debug (para compartir con pocas vueltas)

Si no configurás una firma propia, este proyecto cae automáticamente a
firmar el build `release` con la **misma firma de debug** (ver
`signingConfigs`/`buildTypes` en `pkg/android/phoenix/build.gradle:166-188`
— es un fallback intencional del proyecto). Sirve perfectamente para
instalar en tus propios dispositivos o pasarle el APK a alguien para que
lo pruebe, pero:

- **No es válido para publicar en Google Play** (Play exige una firma de
  release verdadera, no la de debug).
- Cada vez que alguien más compile con SU PROPIA keystore de debug (la que
  Android Studio genera sola en `~/.android/debug.keystore` la primera
  vez), la firma cambia — dos personas compilando este mismo repo no van
  a poder generar APKs "actualizables" entre sí (Android no deja
  instalar una actualización si la firma no coincide con la ya instalada).

Para generarlo así, simplemente:

```bash
./gradlew assembleAarch64Release
```

o desde la IDE: seleccionar variante `aarch64Release` y repetir el paso 4
(Build APK(s), o Run si tenés un dispositivo conectado).

El APK queda en:

```
pkg/android/phoenix/build/outputs/apk/aarch64/release/phoenix-aarch64-release.apk
```

### Opción B — Firma propia (recomendada para distribuir de verdad)

Pensada para cuando vas a repartir el APK más ampliamente o vas a ir
actualizándolo con el tiempo (misma firma entre versiones = Android deja
instalar la actualización encima sin desinstalar).

1. **Generar una keystore propia** (una sola vez; guardala en un lugar
   seguro y hacé backup — si la perdés no vas a poder firmar
   actualizaciones futuras con la misma identidad). Desde una terminal
   con el JDK de Android Studio en el PATH (o usando `keytool` de
   cualquier JDK 17+ instalado):

   ```bash
   keytool -genkey -v -keystore retroarch-release.keystore -alias retroarch -keyalg RSA -keysize 2048 -validity 10000
   ```

   Va a pedir una contraseña para la keystore, otra para la clave (podés
   usar la misma), y algunos datos de identidad (nombre, organización,
   etc. — no son críticos para uso personal).

2. **No la guardes dentro del repo git** — es un secreto. Guardala, por
   ejemplo, en `pkg/android/phoenix/retroarch-release.keystore` y
   confirmá que `.gitignore` la cubra (o agregala vos: `echo
   "*.keystore" >> .gitignore`).

3. `build.gradle` lee las contraseñas como *project properties*
   (`project.hasProperty("RELEASE_STORE_FILE")`, líneas 167 y 178) — pero
   ⚠️ **`pkg/android/phoenix/gradle.properties` ya está versionado en este
   repo** (no está en `.gitignore` como `local.properties`), así que
   escribir contraseñas reales ahí las dejaría a un `git add`/commit de
   distancia de terminar en el historial. Dos formas de evitarlo:

   **Opción recomendada — pasarlas por línea de comandos** (nunca tocan
   un archivo del repo):

   ```bash
   ./gradlew assembleAarch64Release \
     -PRELEASE_STORE_FILE=retroarch-release.keystore \
     -PRELEASE_STORE_PASSWORD=tu_contraseña_de_keystore \
     -PRELEASE_KEY_ALIAS=retroarch \
     -PRELEASE_KEY_PASSWORD=tu_contraseña_de_clave
   ```

   **Alternativa — guardarlas en el `gradle.properties` GLOBAL de tu
   usuario** (`~/.gradle/gradle.properties`, en
   `C:\Users\<usuario>\.gradle\gradle.properties` en Windows — este
   archivo vive fuera del repo, nunca se commitea) en vez del del
   proyecto:

   ```properties
   RELEASE_STORE_FILE=D:/ruta/fuera/del/repo/retroarch-release.keystore
   RELEASE_STORE_PASSWORD=tu_contraseña_de_keystore
   RELEASE_KEY_ALIAS=retroarch
   RELEASE_KEY_PASSWORD=tu_contraseña_de_clave
   ```

   Gradle combina automáticamente las properties globales con las del
   proyecto, así que `project.hasProperty("RELEASE_STORE_FILE")` las ve
   igual sin que el proyecto local se entere. (Para `RELEASE_STORE_FILE`
   con esta alternativa usá una ruta absoluta, ya que no vive junto al
   `build.gradle`.)

   Evitá el camino de "escribirlas en
   `pkg/android/phoenix/gradle.properties` y tener cuidado de no
   commitearlo" — es fácil olvidarse un `git add -A` de por medio.

4. Sincronizar Gradle (`File → Sync Project with Gradle Files`) y generar
   el APK como en la Opción A (`assembleAarch64Release` o Build APK(s)
   con la variante `aarch64Release`) — ahora va a quedar firmado con tu
   keystore propia.

5. Alternativa con asistente gráfico (evita escribir contraseñas en un
   archivo de texto): `Build → Generate Signed Bundle / APK... → APK →
   Next`, elegir "Create new..." para generar la keystore desde ahí mismo
   con un diálogo, completar los datos, elegir la variante `release`, y
   `Finish`. Android Studio guarda la keystore donde indiques y no la deja
   en texto plano en el proyecto.

## 6. Instalar el APK generado

Por USB con el dispositivo conectado:

```bash
adb install -r pkg/android/phoenix/build/outputs/apk/aarch64/release/phoenix-aarch64-release.apk
```

(`-r` reinstala encima de una versión ya instalada, si la firma coincide).

Sin cable: transferir el archivo `.apk` al celular (por cualquier medio —
USB como almacenamiento, Drive, etc.) y abrirlo desde un explorador de
archivos del dispositivo. Android va a pedir habilitar "Instalar apps
desconocidas" para la app que estés usando para abrirlo (una vez por
app), ya que no viene de Google Play.

## 7. Problemas comunes de entorno

### `[CXX1429] ... ERROR: NDK path cannot contain spaces`

El build nativo (`ndk-build`, basado en GNU Make) no tolera espacios en la
ruta del SDK/NDK. Si tu usuario de Windows tiene espacio en el nombre
(por ejemplo `C:\Users\Jean Paul\...`), el SDK por defecto queda instalado
ahí y el build falla con este error, sin importar el código.

**Solución**: `File → Settings → Languages & Frameworks → Android SDK`,
cambiar "Android SDK Location" a una ruta sin espacios (ej.
`D:\Android\Sdk`), aplicar, y dejar que Android Studio vuelva a
descargar el contenido en la nueva ubicación. Confirmar en **SDK Tools**
que el NDK `29.0.14206865` sigue instalado, y volver a sincronizar.

### Compilar desde terminal sin abrir Android Studio: falta `JAVA_HOME`

```
ERROR: JAVA_HOME is not set and no 'java' command could be found in your PATH.
```

Android Studio trae su propio JDK (JBR) que sirve para esto sin instalar
nada aparte:

```bash
JAVA_HOME="/c/Program Files/Android/Android Studio/jbr" ./gradlew assembleAarch64Release
```

(ajustar la ruta si Android Studio está instalado en otro lado).
