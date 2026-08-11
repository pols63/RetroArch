# Cómo compilar y probar en Windows (features homologadas)

> **Estado: pausado (2026-08-09).** El foco activo de este fork sigue
> siendo Android — la homologación a Windows es paridad de UX, no una
> necesidad funcional (en Windows ya es simple reemplazar
> `retroarch.cfg`/cores copiando archivos directo a sus carpetas, sin
> depender del picker nativo). Se dejó de avanzar acá al toparse con que
> el toolchain MinGW sin firmar es bloqueado por Smart App Control (ver
> sección "Problemas comunes" más abajo) — evaluado desactivarlo o cruzar
> a Docker/WSL2 con auto-firma, y por ahora no vale la pena el esfuerzo
> extra. Este doc queda tal cual para retomarlo cuando corresponda.

Guía para el pendiente que quedó documentado en `docs/memory.md` ("Sesión:
rediseño de Archivo de configuración + homologación de bulk-cores a
Windows"): las dos features de `dev-masscores` se extendieron a Windows
con código escrito y revisado a mano, pero **nunca se compilaron ni se
probaron en un Windows real** — no había MSVC ni MinGW/gcc disponibles en
esa sesión. Este doc cubre justamente eso: compilar por primera vez y
validar manualmente ambas features.

Features a probar:

1. **"Archivo de configuración" → Importar/Exportar** con el picker
   nativo de Windows (`GetOpenFileName`/`GetSaveFileName` en
   `ui/drivers/ui_win32.c`).
2. **Manage Cores → Install Cores from Folder (Bulk) / Backup Cores**,
   con el picker de carpetas nativo (`SHBrowseForFolderW`) y el filtro de
   extensión `.dll` (en vez de `.so` como en Android).

## 1. Prerrequisitos: toolchain MinGW vía MSYS2

Este fork compila para Windows con GCC/MinGW a través de `Makefile.win`
(`make -f Makefile.win`, ver `CLAUDE.md` sección "Desktop / other
platforms") — **no** con MSVC. La forma estándar de tener ese toolchain en
Windows es [MSYS2](https://www.msys2.org/):

1. Instalar MSYS2 (instalador oficial, ruta sin espacios recomendado, p.
   ej. `C:\msys64`).
2. Abrir específicamente la terminal **"MSYS2 MINGW64"** del menú
   inicio (no "MSYS2 MSYS", no "MINGW32" salvo que quieras un build de
   32 bits) — es la que trae el compilador de 64 bits en el `PATH`.
3. Actualizar el sistema base (puede pedir cerrar y reabrir la terminal a
   mitad de camino, es normal):
   ```bash
   pacman -Syu
   ```
4. Instalar el toolchain y las dependencias que este `Makefile.win` pide
   (`freetype`, compresión para `HAVE_7ZIP`, git y make si no los tenés
   ya):
   ```bash
   pacman -S --needed base-devel mingw-w64-x86_64-toolchain \
     mingw-w64-x86_64-freetype mingw-w64-x86_64-zlib \
     mingw-w64-x86_64-make git make
   ```

## 2. Compilar

Desde la terminal **MSYS2 MINGW64**, navegar al repo (las rutas de
Windows se acceden como `/d/...`):

```bash
cd /d/Coding/MyRetroarch
make -f Makefile.win -j$(nproc)
```

Esto genera `retroarch.exe` en la raíz del repo. Si aparecen errores de
compilación en el código nuevo de Windows (Feature 1 o 2), son justamente
lo que este build está buscando detectar por primera vez — revisar el
archivo/línea que reporta GCC, no asumir que el problema es del entorno.

### Problemas comunes

- **Error de link `-lcg`/`-lcgGL` (Cg Toolkit no encontrado)**: el
  `Makefile.win` de este repo trae `HAVE_CG := 1` por defecto, y el Cg
  Toolkit de NVIDIA no es algo que MSYS2 empaquete. Si no lo tenés
  instalado, compilar sin esa opción:
  ```bash
  make -f Makefile.win HAVE_CG=0 -j$(nproc)
  ```
- **`freetype2/...h: No such file or directory`**: falta el paquete
  `mingw-w64-x86_64-freetype` del paso 1, o se abrió una terminal MSYS2
  que no es la variante MINGW64.
- **El `.exe` no arranca fuera de la terminal MSYS2** (falta una DLL como
  `libfreetype-6.dll`, `libwinpthread-1.dll`, etc.): normal, esas DLLs
  viven en `C:\msys64\mingw64\bin`. Para probar rápido, ejecutar
  `./retroarch.exe` **desde la misma terminal MSYS2 MINGW64** (tiene ese
  directorio en el `PATH`). Para un `.exe` standalone que corra por doble
  clic, copiar las DLLs que pida (`ldd retroarch.exe` desde MSYS2 lista
  cuáles) a la misma carpeta que `retroarch.exe`.

## 3. Preparar el entorno de prueba

RetroArch en Windows corre en **modo portable** si hay un `retroarch.cfg`
junto al `.exe` — así toda la config/cores/assets quedan contenidos en la
misma carpeta del build, sin tocar `%APPDATA%`, más simple para iterar:

```bash
touch retroarch.cfg   # si no existe ya uno de un intento previo
mkdir -p cores test_cores_dll
```

- `cores/` — donde RetroArch busca cores por defecto en modo portable;
  copiar ahí 1-2 cores `.dll` cualesquiera (de una instalación existente
  de RetroArch en este PC, o descargados una vez desde Online Updater con
  un build oficial) para tener algo con qué probar la Feature 2.
- `test_cores_dll/` — carpeta aparte con un par de `.dll` de cores (pueden
  ser copias de los mismos), para usar como origen del picker de "Install
  Cores from Folder (Bulk)" sin mezclarla con `cores/` (así se nota si
  realmente los copió ahí).

## 4. Checklist de pruebas manuales

### Feature 1 — Configuration File Import/Export

- [ ] Menú principal → **Configuration File** → **Export** → se abre el
      diálogo nativo `GetSaveFileName` (no el navegador interno de
      RetroArch) → guardar en cualquier ruta.
- [ ] Cambiar algo trivial en Settings (p. ej. un hotkey) y guardar la
      config actual.
- [ ] **Import** el archivo exportado en el paso anterior → diálogo nativo
      `GetOpenFileName` → confirmar el diálogo de "¿sobreescribir config
      actual?" (si aplica) → verificar que el hotkey cambiado en el paso
      anterior efectivamente se restauró al valor del archivo importado.
- [ ] Cerrar y reabrir RetroArch → el cambio importado persiste (no se
      pierde al reiniciar).

### Feature 2 — Bulk Install / Backup de cores

- [ ] **Manage Cores → Install Cores from Folder (Bulk)** → se abre
      `SHBrowseForFolderW` (diálogo nativo de selección de carpeta, no un
      picker de archivo) → seleccionar `test_cores_dll/`.
- [ ] Aparece la pantalla de confirmación (cantidad de cores detectados)
      antes de instalar — confirma que el filtro de extensión `.dll`
      encontró los archivos (si aparece "0 cores encontrados", ver nota
      de abajo).
- [ ] Confirmar instalación → los `.dll` aparecen copiados en `cores/` y
      listados en **Manage Cores** con nombre reconocido (no "License:
      N/A" — si aparece eso, falta la carpeta `info` con los `.info`
      correspondientes, no es un bug de esta feature, ver
      `docs/memory.md`).
- [ ] **Manage Cores → Backup Cores** con una carpeta destino nueva →
      confirmar que los `.dll` instalados se copian ahí.
- [ ] Repetir "Install All" con una carpeta de **varios** `.dll` seguidos
      (no solo uno) para estresar el flujo asíncrono, igual que se probó
      en Android — es donde apareció el crash de UAF documentado en
      `docs/memory.md` (`task_core_backup_finder`); confirmar que en
      Windows tampoco crashea.

Si "0 cores encontrados" con archivos `.dll` reales en la carpeta: es la
señal exacta del bug que ya se corrigió en código
(`CORE_BULK_SUFFIX_PLAIN` hardcodeado a `.so`) — si reaparece, revisar que
el build efectivamente compiló con el fix (`#ifdef ANDROID` → `.so`,
`#elif defined(_WIN32)` → `.dll]` en `tasks/task_core_bulk_install.c` /
`task_core_bulk_backup.c`).

## 5. Después de probar

Actualizar `docs/memory.md` con el resultado (confirmado funcionando /
qué falló y dónde) — es lo que ese doc está esperando en su sección de
"Próximos pasos" para poder marcar como cerrado el pendiente de Windows.
Si algo falló y se corrigió, commitear el fix por separado del resto del
trabajo de sync con upstream (ver
`docs/retroarch-android-sync-upstream.md`), para no mezclar "fix de
feature propia" con "traer cambios de terceros" en el mismo commit.
