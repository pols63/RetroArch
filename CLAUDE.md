# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

A personal fork of RetroArch (upstream: `libretro/RetroArch`, origin here:
`pols63/RetroArch`) — the reference frontend for the libretro API. Active
development on this fork happens on the `dev-masscores` branch and is
currently focused on the **Android port**. See `docs/memory.md` for the
state of the current feature branch (bulk core install/backup via SAF) and
`docs/retroarch-android-bulk-cores.md` / `docs/retroarch-android-bulk-cores-testing.md`
for the spec and the build/test walkthrough for that feature — read those
before starting new Android work, they contain hard-won environment
troubleshooting (NDK path restrictions, SAF/VFS bridge location, etc.).

RetroArch itself: a single native C frontend that loads emulator/game-engine
backends ("libretro cores") as dynamic libraries and provides
video/audio/input/menu around them. It ships with in-tree menu drivers
(RGUI, XMB, Ozone, MaterialUI/GLUI), a VFS layer, and a task system for
async work (downloads, decompression, core install, etc.).

## Building

### Android (primary target for this fork)

The Android project is `pkg/android/phoenix/` (a Gradle project, **not** the
repo root — always `File → Open` that specific folder in Android Studio).
Native code is built via `ndk-build` against
`pkg/android/phoenix-common/jni/Android.mk`, which compiles
`griffin/griffin.c` — a single unity-build file that `#include`s the actual
`.c` sources. When the Android build fails with a compile error, the error
will point at the real source file/line; when adding new native source
files to the Android build, they must be registered as an `#include` inside
`griffin/griffin.c` (this fork already does this for the bulk-cores task
files — use that as the template).

Build variants: use **`aarch64Debug`** for testing on a real modern phone.
Avoid the `playStoreNormal`/`playStorePlus` variants unless specifically
testing Play Feature Delivery — they pull in extra machinery (on-demand
core download via Play Asset Delivery) that isn't needed for normal
development.

```bash
cd pkg/android/phoenix
./gradlew assembleAarch64Debug
adb install -r build/outputs/apk/aarch64/debug/phoenix-aarch64-debug.apk
```

Prerequisites: `compileSdk`/`targetSdk 36`, NDK `29.0.14206865` pinned in
`pkg/android/phoenix/build.gradle`, JDK 17+. Prefer Android Studio over raw
`gradlew` — it resolves the SDK/NDK without manual `ANDROID_HOME` setup.

**Known environment gotcha**: `ndk-build` (GNU Make based) fails with
`[CXX1429] NDK path cannot contain spaces` if the Android SDK lives under a
path with spaces (e.g. a Windows user directory like `C:\Users\Jean
Paul\...`). Fix by moving the SDK to a space-free path and repointing it in
both `File → Settings → Languages & Frameworks → Android SDK` *and*
`File → Project Structure → SDK Location` (the project's own
`local.properties` overrides the global IDE setting).

Debug via `adb logcat`; this fork's bulk-core feature logs under the tags
`Core Bulk Install` / `Core Bulk Backup`.

### Desktop / other platforms

Standard GNU-make build for Unix-likes from the repo root:

```bash
./configure   # optional, generates config.mk
make -j$(nproc)
```

Platform-specific variants are selected via `Makefile.<platform>` (e.g.
`Makefile.win`, `Makefile.switch`, `Makefile.wiiu` — see the many
`Makefile.*` files at the repo root). `config.def.h` holds compile-time
defaults; don't edit it directly for a local preference — override at
runtime via `retroarch.cfg` or at build time via a local `Makefile.local`
(gitignored, sits next to `Makefile`).

## Architecture

- **Menu drivers** (`menu/drivers/`): RGUI, XMB, Ozone, MaterialUI (GLUI).
  Selected/compiled in via `HAVE_RGUI`/`HAVE_XMB`/`HAVE_OZONE`/`HAVE_MATERIALUI`.
  New menu entries go through `menu/menu_displaylist.c` (list population),
  `menu/cbs/menu_cbs_ok.c` (OK/confirm callbacks),
  `menu/cbs/menu_cbs_deferred_push.c` (deferred list population), and
  `menu/cbs/menu_cbs_sublabel.c` (descriptions) — a new menu action
  typically touches all four plus a label constant in `msg_hash.h` /
  `msg_hash_lbl_str.h` / `intl/msg_hash_us.h`.
- **Task system** (`tasks/`): async work (downloads, decompression, core
  install/backup, etc.) runs as `retro_task_t` pushed via `task_push_*()`
  functions and driven to completion off the main thread; see
  `tasks/task_decompress.c` and `tasks/task_core_backup.c` for the pattern
  this fork's bulk-install/backup tasks (`tasks/task_core_bulk_install.c`,
  `tasks/task_core_bulk_backup.c`) follow. Prefer reusing an existing
  `task_push_*` entry point over reimplementing install/copy logic.
- **VFS / SAF bridge (Android)**: `frontend/drivers/platform_unix.c` hosts
  the JNI glue; on Android 10+ scoped storage is handled through Storage
  Access Framework (`saf://` paths), with a native↔Java bridge
  (`android_show_saf_tree_picker()` / `safTreeAdded`) already wired up — the
  VFS layer reads/writes `saf://` paths transparently, so new
  SAF-driven features should go through that existing bridge rather than a
  fresh `DocumentFile`-based Kotlin/Java implementation.
- **Bundled assets extraction (Android)**: menu theme assets (icons, TTF
  fonts, wallpapers — the `libretro/retroarch-assets` content) are baked
  into the APK's own `assets/` folder at build time (Gradle `sourceSets`
  in `pkg/android/phoenix/build.gradle`) and self-extracted to the app's
  private data dir on first boot / APK version bump by
  `menu/menu_driver.c` (`rarch_menu_init` → `task_push_decompress`), driven
  by config keys `bundle_assets_*` written from
  `pkg/android/phoenix-common/src/.../UserPreferences.java`. The extractor
  strips the `assets/` prefix from each archive entry
  (`tasks/task_decompress.c:file_decompressed_subdir`), so bundled content
  lands flat under the app's data dir — `frontend/drivers/platform_unix.c`'s
  `DEFAULT_DIR_ASSETS` default must point at that same data dir (not a
  `.../assets` subdirectory) to match.
- **Config**: compile-time defaults in `config.def.h`; runtime persisted to
  `retroarch.cfg` (`configuration.c` owns the `SETTING_*` registration
  macros). A key only gets its compiled-in default applied the first time
  it's written to a config file — once a key exists in a user's
  `retroarch.cfg`, later default changes in `config.def.h`/platform driver
  code won't retroactively fix an existing install; a clean uninstall
  (Android) or config reset is needed to pick up a corrected default.

## Coding guidelines

Full guide: `CODING-GUIDELINES` and https://docs.libretro.com/development/coding-standards/.
Highlights (this is a large, portable-to-embedded-targets C codebase — these
are load-bearing, not stylistic preference):

- C89-compatible C source: declare variables at the top of a function or
  block (no C99 mid-block/`for`-loop declarations), no VLAs.
- Allman brace style; single-statement blocks don't get braces (unless a
  multi-line macro requires it); prefer `for (;;)` over `while (true)`.
- Avoid tiny one-line getter/setter functions — a function should justify
  its call overhead.
- Struct members ordered largest-alignment-first (see `CODING-GUIDELINES`
  for the full type ordering table); interleave pointer + size-variable
  pairs.
- Be conservative with stack usage — some targets have ~128KB stacks.
