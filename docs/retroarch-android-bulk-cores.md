# Instructivo: Instalación y Backup Masivo de Cores en RetroArch Android (fork personal)

## Contexto y objetivo

RetroArch para Android restringe el acceso directo a su carpeta privada de cores
(`/data/user/0/com.retroarch/files/cores/` o similar) debido a las políticas de
Scoped Storage de Android 10+. Esto es correcto y **no se debe modificar** —
la carpeta de cores debe seguir siendo privada de la app para evitar inyección
de código vía reemplazo de librerías `.so` por parte de otras apps.

Sin embargo, el flujo actual para restaurar un backup de cores es tedioso:
solo permite seleccionar **un archivo `.so` a la vez** mediante el navegador
de archivos interno de RetroArch, que además es incómodo de usar. No existe
opción de "instalar todos los cores de esta carpeta" ni de "backup masivo".

Esta limitación es un feature request real y abierto en el proyecto oficial
(issue [libretro/RetroArch#16489](https://github.com/libretro/RetroArch/issues/16489),
"Install or Restore all Cores from a Directory"), sin implementar a la fecha.

**Objetivo de este fork:** agregar dos funcionalidades nuevas, reutilizando
al máximo la lógica de instalación de cores que RetroArch ya trae, sin tocar
la carpeta privada de cores ni requerir `MANAGE_EXTERNAL_STORAGE`:

1. **Instalación masiva de cores** desde una carpeta elegida por el usuario
   vía el selector nativo de Android (Storage Access Framework), iterando
   sobre cada `.so` encontrado y llamando a la función de instalación nativa
   existente, una vez por archivo.
2. **Backup de cores**: copiar los cores actualmente instalados hacia una
   carpeta de destino elegida por el usuario, también vía SAF.

## Restricciones de diseño (no negociables)

- **No modificar la ubicación ni permisos de la carpeta privada de cores.**
  Sigue siendo de acceso exclusivo de la app.
- **No solicitar `MANAGE_EXTERNAL_STORAGE`.** Todo el acceso a almacenamiento
  compartido se hace exclusivamente vía `ACTION_OPEN_DOCUMENT_TREE` (SAF),
  con permiso persistente (`takePersistableUriPermission`) solo sobre el
  árbol que el usuario elige explícitamente.
- **No reimplementar la lógica de instalación de un core.** Ubicar la función
  nativa/Java existente que ya recibe un path de archivo y realiza la
  instalación (incluyendo cualquier validación de arquitectura, extracción
  si aplica, etc.), y **llamarla tal cual**, una vez por archivo `.so`
  encontrado en la carpeta elegida.
- **Todo el flujo se dispara explícitamente por el usuario.** Nada de
  watchers en background ni sincronización automática de una carpeta.
- **El backup solo puede leer la subcarpeta de cores del directorio privado
  de la app.** No exponer la carpeta de configs, saves, ni el directorio
  privado completo como origen de backup.

## Fase 0: Investigación del código actual (hacer primero, no asumir nombres)

El código de instalación de cores vive principalmente en la capa nativa C del
menú de RetroArch (no en la capa Java/Phoenix legacy, que hoy es solo un
shell de Activity). Antes de escribir código nuevo:

1. Clonar el fork y ubicar los siguientes archivos como punto de partida de
   búsqueda:
   - `menu/menu_displaylist.c` — construye las listas de menú, incluida la
     opción "Instalar o restaurar un núcleo".
   - `menu/cbs/menu_cbs_ok.c` — contiene los callbacks que se disparan al
     confirmar una selección en el menú (buscar algo como
     `action_ok_core_deferred_set` o similar relacionado a instalar/cargar
     un core desde archivo).
   - `menu/cbs/menu_cbs_deferred_push.c` — cómo se puebla el listado de
     archivos del navegador interno al entrar a "Instalar o restaurar un
     núcleo".
   - `tasks/task_file_transfer.c` y `tasks/task_powerstate.c` (o el archivo
     de tasks correspondiente a instalación local) — puede haber una task
     asíncrona de instalación en vez de una llamada directa.
   - Directorio `pkg/android/` — capa Java/Kotlin mínima que hostea la
     `NativeActivity`; ahí es donde se agregan los `Intent` de SAF y el
     puente JNI hacia el código nativo.

2. Grep útiles para acelerar la ubicación exacta (los nombres de símbolos
   cambian entre versiones, así que verificar contra el HEAD del fork):
   ```
   grep -rn "install_core" --include="*.c" --include="*.h" .
   grep -rn "MSG_CORE_INSTALLED\|core_installed\|CORE_UPDATER" --include="*.c" .
   grep -rn "MANAGE_CORES\|manage_core" --include="*.c" --include="*.h" .
   ```

3. Documentar en un comentario al inicio del PR/commit qué función concreta
   se identificó como "instalar un core desde un path local", con su firma
   exacta, para referencia futura (los issues del repo oficial confirman que
   varias versiones han tenido bugs de instalación, así que vale la pena
   anotar en qué versión/commit se basó esta implementación).

## Fase 1: Instalación masiva vía SAF

### 1.1 Nuevo punto de entrada en el menú

Agregar una entrada nueva junto a "Instalar o restaurar un núcleo" (visible en
la captura de pantalla del menú "Administrar núcleos"), por ejemplo
"Instalar cores desde carpeta (masivo)". Debe:

- Disparar un `Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)` desde la capa
  Java/Kotlin de `pkg/android/`.
- Puentear el resultado (el `Uri` del árbol elegido) hacia el código nativo
  vía JNI, o resolver todo el procesamiento en Java/Kotlin y solo invocar
  la función nativa de instalación por archivo vía JNI (preferible, para no
  tener que portar lógica de filtrado a C).

### 1.2 Lógica de recorrido y copia a temporal

En Kotlin/Java, al recibir el `Uri` del árbol:

```kotlin
val treeUri = data?.data ?: return
contentResolver.takePersistableUriPermission(
    treeUri,
    Intent.FLAG_GRANT_READ_URI_PERMISSION
)

val treeDoc = DocumentFile.fromTreeUri(context, treeUri)
val soFiles = treeDoc?.listFiles()?.filter {
    it.isFile && it.name?.endsWith("_libretro_android.so") == true
} ?: emptyList()
```

**Nota sobre el filtro de nombre:** usar el sufijo real que usan los cores de
Android (verificar contra los nombres ya presentes en el directorio privado
de cores del dispositivo de prueba — suelen terminar en
`_libretro_android.so`), no solo `.so` genérico, para reducir de entrada la
superficie de "archivos irreconocibles" antes de siquiera mostrarlos al
usuario.

Por cada archivo encontrado, copiar a un temporal en `cacheDir` y llamar a la
función de instalación nativa ya existente (identificada en la Fase 0):

```kotlin
val results = mutableListOf<Pair<String, Boolean>>()

for (doc in soFiles) {
    val tempFile = File(context.cacheDir, doc.name!!)
    contentResolver.openInputStream(doc.uri)?.use { input ->
        tempFile.outputStream().use { output -> input.copyTo(output) }
    }

    val ok = nativeInstallCoreFromPath(tempFile.absolutePath) // función existente, vía JNI

    results.add(doc.name!! to ok)
    tempFile.delete() // limpiar temporal siempre, incluso si falló
}
```

### 1.3 Confirmación y resumen (no instalación silenciosa)

- Antes de ejecutar el loop: mostrar un diálogo con el conteo de archivos
  detectados y cuántos de ellos ya existen como core instalado (mismo
  nombre), indicando que serán sobrescritos. Un solo diálogo de
  confirmación para todo el lote, no uno por archivo.
- Después del loop: mostrar resumen final — instalados OK, fallidos, y el
  detalle de cuáles fallaron (nombre de archivo), reutilizando el sistema de
  notificaciones/toast que ya usa RetroArch para "Core instalado" /
  "Fallo al instalar core".
- Loggear cada instalación (archivo, resultado, timestamp) usando el sistema
  de logging existente de RetroArch (`RARCH_LOG` / Logcat), no un log nuevo
  paralelo.

## Fase 2: Backup de cores

### 2.1 Nuevo punto de entrada

Agregar entrada "Backup de cores" en el mismo submenú. Al seleccionarla:

- Disparar `Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)` para elegir carpeta de
  **destino** (no origen — el origen está fijo y es la carpeta privada de
  cores).
- Persistir el permiso del árbol destino igual que en la Fase 1.

### 2.2 Copia

El origen es exclusivamente el directorio privado de cores de la app
(`context.getExternalFilesDir(null)/cores/` o el path interno equivalente
que uses internamente — confirmar el path exacto que usa la build actual en
`RARCH_DIR_CORE`, no asumir). **No permitir seleccionar el origen ni ofrecer
backupear ningún otro subdirectorio de la app.**

```kotlin
val coresDir = File(context.getExternalFilesDir(null), "cores")
val destTree = DocumentFile.fromTreeUri(context, destUri)!!

var copied = 0
coresDir.listFiles { f -> f.isFile && f.name.endsWith(".so") }?.forEach { core ->
    val destFile = destTree.createFile("application/octet-stream", core.name)
    destFile?.uri?.let { uri ->
        contentResolver.openOutputStream(uri)?.use { output ->
            core.inputStream().use { input -> input.copyTo(output) }
        }
        copied++
    }
}
```

### 2.3 Resumen

Igual que en la Fase 1: un diálogo de confirmación antes (cantidad de cores a
copiar, tamaño total aproximado) y un resumen al terminar (copiados OK /
fallidos), sin diálogos por archivo individual.

## Fase 3: Checklist de validación antes de dar por cerrado el cambio

- [ ] La carpeta privada de cores sigue sin ser accesible por otras apps
      (no se tocó ningún permiso ni ubicación de esa carpeta).
- [ ] No se agregó `MANAGE_EXTERNAL_STORAGE` al manifest.
- [ ] La instalación masiva reutiliza la función nativa existente de
      instalar-core-desde-path, sin lógica de instalación duplicada.
- [ ] El filtro de archivos rechaza (o al menos marca como "no reconocido"
      con confirmación extra) cualquier archivo cuyo nombre no siga el
      patrón de core válido.
- [ ] Los temporales en `cacheDir` se eliminan siempre, incluso si la
      instalación de ese archivo específico falla (usar `try/finally`).
- [ ] Hay un solo diálogo de confirmación por lote, no uno por archivo.
- [ ] El backup solo puede leer desde la carpeta de cores privada, nunca
      desde configs/saves/todo el directorio de la app.
- [ ] Ambos flujos (instalar masivo, backup) se disparan solo por acción
      explícita del usuario — nada corre en background ni al iniciar la app.
- [ ] Se registra en log (Logcat/RARCH_LOG) cada archivo procesado con su
      resultado.

## Notas finales para Claude Code

- Priorizar grep/lectura del código real del fork sobre los nombres de
  función mencionados en este documento — son referencias orientativas
  para acelerar la búsqueda, no nombres confirmados contra el HEAD actual.
- Si la función nativa de instalación resulta estar detrás de una task
  asíncrona (patrón común en RetroArch para operaciones de archivo), el
  loop de la Fase 1.2 debe esperar la finalización de cada task antes de
  procesar el siguiente archivo (o encolarlas y agregar callback de
  finalización), para no saturar la cola de tasks con N operaciones
  concurrentes sobre el mismo directorio de cores.
- Mantener los strings de UI nuevos en el mismo sistema de i18n
  (`intl/msg_hash_*.c` / `msg_hash.h`) que usa el resto del menú, en vez de
  strings hardcodeados, para que el fork sea fácil de mantener contra
  updates futuros del upstream.
