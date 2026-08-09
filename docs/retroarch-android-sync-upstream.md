# Cómo actualizar este fork desde upstream (libretro/RetroArch)

Guía para traer los cambios oficiales de `libretro/RetroArch` a este fork
sin perder las features propias (`dev-masscores`: bulk install/backup de
cores, menú de Configuration File con Import/Export nativo), y para
mantener los assets del menú **embebidos en el APK** (objetivo explícito:
que el usuario final no tenga que descargarlos después de instalar, vía
Online Updater).

Diagnóstico de partida (verificado en esta sesión):

- `origin` (`pols63/RetroArch`) es el único remoto configurado. Su rama
  `master` es un **mirror limpio** de `libretro/RetroArch` — sin commits
  propios, solo desactualizado.
- `dev-masscores` tiene sus commits propios por encima de `master` y ya
  está publicada en `origin/dev-masscores`.
- Los assets del menú (iconos/TTF/wallpapers de `libretro/retroarch-assets`)
  se embeben en el APK vía `pkg/android/phoenix/build.gradle:136`
  (`assets.srcDirs = ['assets', '../../../media/assets']`) y se
  auto-extraen en el dispositivo la primera vez / en cada actualización de
  versión (ver paso 4 — es automático, no hace falta tocar código para
  que se refresquen).
- `media/assets` **no está versionada en este repo** (gitignored a
  propósito, es contenido binario pesado) — se puebla localmente corriendo
  `./fetch-submodules.sh` desde la raíz. Verificado: en este entorno la
  carpeta ya existe con contenido pero **no es un clon git real** (no
  tiene `.git` propio), así que no se puede actualizar con `git pull`
  directo — ver paso 4.

## 0. Preparación

```bash
git status                 # confirmar árbol de trabajo limpio
git branch dev-masscores-backup-$(date +%Y%m%d)   # red de seguridad opcional
```

Si `git status` muestra cambios sin commitear, commitealos o guardalos
(`git stash -u`) antes de seguir — el merge de abajo puede generar
conflictos y es más fácil resolverlos sobre un árbol limpio.

## 1. Traer los cambios oficiales a `master`

Este repo no tiene agregado el remoto de upstream todavía. Agregarlo una
sola vez:

```bash
git remote add upstream https://github.com/libretro/RetroArch.git
git fetch upstream
```

Traer los cambios a la `master` local (al ser un mirror sin commits
propios, esto siempre es fast-forward):

```bash
git checkout master
git merge --ff-only upstream/master
```

Si por algún motivo `--ff-only` falla (algo le agregó un commit propio a
`master`), parar y revisar antes de forzar nada.

## 2. Mergear `master` en `dev-masscores`

**Usar `merge`, no `rebase`** — `dev-masscores` ya está publicada en
`origin/dev-masscores`; un rebase reescribiría el historial y forzaría un
push forzado, riesgoso si alguien más la tiene clonada.

```bash
git checkout dev-masscores
git merge master
```

### Dónde esperar conflictos

Los commits propios tocan mayormente archivos nuevos o aislados
(`tasks/task_core_bulk_install.c`, `tasks/task_core_bulk_backup.c`,
`ui/drivers/ui_win32.c`), donde no debería haber fricción. El riesgo real
está en archivos compartidos donde el fork solo *agrega* líneas, y
upstream también los toca seguido:

- `griffin/griffin.c` (los `#include` nuevos de las tasks del fork)
- `Makefile.common` (registro de `task_core_bulk_install.c` /
  `task_core_bulk_backup.c` para el build de Windows)
- `config.def.h`, `msg_hash.h`, `msg_hash_lbl_str.h`, `intl/msg_hash_us.h`
- `menu/menu_displaylist.c`, `menu/cbs/menu_cbs_ok.c`,
  `menu/cbs/menu_cbs_deferred_push.c`, `menu/cbs/menu_cbs_sublabel.c`
- `frontend/drivers/platform_unix.c` — **atención especial**: este archivo
  tiene el fix del default de `assets_directory` (para que apunte a
  `app_dir` sin el `/assets` extra, ver `docs/memory.md`). Si upstream
  también tocó ese bloque, resolver el conflicto preservando el default
  corregido del fork, no el de upstream.
- `pkg/android/phoenix/build.gradle` — confirmar que tras el merge la
  línea 136 siga teniendo `assets.srcDirs = ['assets',
  '../../../media/assets']` (upstream no debería tocar este archivo del
  todo, pero conviene revisar el diff igual).

Para cada conflicto: revisar con `git diff` cuál lado es el cambio de
upstream vs. cuál es la línea agregada por el fork, y combinar ambos
(normalmente no son cambios excluyentes, son inserciones cercanas).

## 3. Actualizar los assets embebidos (`media/assets`)

Como `media/assets` en este entorno no es un clon git real, la forma más
simple y segura de actualizarla es re-descargarla entera:

```bash
mv media/assets media/assets.bak   # por si acaso; borrar al final si todo salió bien
./fetch-submodules.sh
```

Esto re-clona `libretro/retroarch-assets` (y de paso refresca
`media/shaders_cg`, `media/overlays`, `media/autoconfig`,
`media/libretrodb` — ninguno de esos otros cuatro está declarado en los
`sourceDirs` de Android, así que no afectan lo que va embebido en el APK,
pero no está de más tenerlos al día si se usan en otras plataformas).

Verificar que quedó bien poblada antes de borrar el backup:

```bash
ls media/assets   # debería verse: branding ctr fonts glui nxrgui ozone pkg rgui scripts sounds src switch wallpapers xmb COPYING Makefile README.md configure
rm -rf media/assets.bak
```

**Nota para el futuro**: si querés poder actualizar esta carpeta con
`git pull` en vez de re-clonar cada vez, convertila en un clon real una
sola vez:

```bash
rm -rf media/assets
git clone https://github.com/libretro/retroarch-assets.git media/assets
```

A partir de ahí, actualizar assets en el futuro es solo:

```bash
git -C media/assets pull
```

## 4. Confirmar que el re-empaquetado es automático

No hace falta tocar código ni configuración para que un dispositivo con
la app ya instalada recoja los assets nuevos: `versionCode` en
`pkg/android/phoenix/build.gradle:49` se genera con el timestamp Unix del
momento del build (`System.currentTimeMillis() / 1000`), así que **todo
build nuevo tiene un `versionCode` mayor al anterior**. RetroArch compara
ese valor (`bundle_assets_extract_version_current`, leído del
`PackageManager` en `UserPreferences.java`) contra el que quedó guardado
en `retroarch.cfg` del install anterior
(`bundle_assets_extract_last_version`) y, si difieren, vuelve a extraer
el contenido embebido del APK — ver `menu/menu_driver.c:3773-3794`. Es
decir: alcanza con reinstalar/actualizar el APK para que los assets
nuevos reemplacen a los viejos en el dispositivo, sin necesitar
desinstalación completa (esa sí hace falta solo para el bug de
`assets_directory` ya corregido, no para esto).

## 5. Recompilar y validar en dispositivo

```bash
cd pkg/android/phoenix
./gradlew assembleAarch64Debug
adb install -r build/outputs/apk/aarch64/debug/phoenix-aarch64-debug.apk
```

Checklist mínimo post-sync (además de correr el smoke test de
`docs/retroarch-android-bulk-cores-testing.md`):

- [ ] El menú carga con iconos/fuente TTF normales (XMB/Ozone/MaterialUI),
      no bitmap pixelado — confirma que los assets nuevos se embebieron y
      extrajeron bien.
- [ ] "Install All" / bulk backup siguen funcionando sin crashear.
- [ ] Import/Export de Configuration File (picker nativo) sigue
      funcionando.
- [ ] `adb logcat` sin errores nuevos relacionados a `Core Bulk Install` /
      `Core Bulk Backup` durante el smoke test.

## 6. Commit y push

```bash
git add -A
git commit -m "sync: actualizar desde upstream libretro/RetroArch"
git push origin dev-masscores
```

Revisar `git status`/`git diff --stat` antes del `add -A` por si el merge
trajo algo inesperado (por ejemplo, si `media/assets.bak` quedó sin
borrar).
