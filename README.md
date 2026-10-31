# OpenGOAL Jak 2 — native macOS ARM64 and iOS

<p align="center">
  <img src="docs/img/jak2-arm64-preview.png" alt="Jak 2 running in the native ARM64 macOS snapshot" width="960">
</p>

This repository is a focused development snapshot for running **Jak 2 from OpenGOAL natively on Apple Silicon**, without Rosetta. It is not a general OpenGOAL trilogy mirror or a Jak 1/Jak 3 project.

This is the `ios` branch of a fork of [DiMiTriFrog/jak2-macos-arm64](https://github.com/DiMiTriFrog/jak2-macos-arm64). The upstream project keeps mobile ports out of scope, so the iOS port lives here. The `main` branch stays identical to upstream.

## Scope

- Native macOS ARM64 runtime and compiler path for Jak 2.
- iOS port for iPhone, iPad and the iOS Simulator (iOS 17 or later), built from this branch.
- Existing x86-64 compatibility for shared infrastructure.
- SDL/OpenGL renderer on desktop, and the same renderer on OpenGL ES 3 through ANGLE (Metal) on iOS.
- Tests, build presets and documentation target Jak 2 unless explicitly marked as shared infrastructure.

The repository does not include game ISOs or proprietary game assets, and no prebuilt iOS app is distributed. Use your own legally obtained Jak 2 copy for local extraction and testing. Never commit or share the iOS data pack, because it contains your game data.

## Build on Apple Silicon

```sh
cmake -B build-arm64-release --preset=Release-macos-arm64-clang
cmake --build build-arm64-release --parallel "$(sysctl -n hw.logicalcpu)"
```

Copy your game files into `iso_data/`, then select the only supported game profile before extracting or compiling game data:

```sh
task set-game-jak2
task set-decomp-ntscv1
task extract
```

Compile the game from the REPL by running `(mi)`:

```sh
task repl
```

Then start the runtime from a second terminal:

```sh
task boot-game
```

The ARM64 path must be launched natively. A translated process under Rosetta is rejected by the runtime.

## Run on iOS

iOS does not allow an app to generate executable code at runtime, so the JIT used on macOS is not available. The iOS build links all GOAL code into the signed app ahead of time (AOT) and draws through ANGLE.

### Requirements

- A completed macOS build, with the game extracted and compiled as described above.
- Xcode, plus these Homebrew packages:

  ```sh
  brew install ninja glslang spirv-cross openssl@3
  ```

- A prebuilt ANGLE from [kivy/angle-builder](https://github.com/kivy/angle-builder), which is not part of this repository. The scripts expect it in `~/jak-ios/deps/angle/chromium-7151_rev1/`, with an `iphonesimulator-arm64` and an `iphoneos-arm64` folder. To use another location, set `ANGLE_DIR` to the folder for your target.

### 1. Generate the signed code

```sh
scripts/build_jak2_aot_macos.sh
```

This also builds a macOS version without JIT in `build-arm64-aot/`. The script stops if any object was compiled with the old `literal` code model. To fix that, run `(make-group "all-code" :force #t)` and then `(mi)` in the REPL.

### 2. Run in the Simulator

```sh
scripts/run_jak2_ios_simulator.sh "iPad Pro 11-inch (M5)"
```

The simulator name is optional and the example above is the default. The app reads the game data from this checkout and starts with `-nosound`, so this script runs the game muted.

### 3. Install on a device

Prepare the device once:

1. Connect it with a cable, unlock it and trust this Mac.
2. Turn on **Settings > Privacy & Security > Developer Mode**.
3. After the first install, trust the developer in **Settings > General > VPN & Device Management**.

Then build, sign, install and launch, using your own Apple team ID and a bundle ID of your choice:

```sh
TEAM_ID=YOURTEAMID BUNDLE_ID=com.example.jak2 scripts/install_jak2_ios_device.sh
```

- **Several devices connected:** pass the device name or UDID as an argument.
- **Provisioning:** creating a profile also needs `xcodegen` (`brew install xcodegen`). When the keychain asks to let `codesign` use the development key, answer **Always Allow**.
- **Free Apple teams:** profiles last 7 days, so rerun the script to renew them.
- **Game data:** the first install creates and copies a data pack of about 2.7 GB into the app's `Documents/jak2`. To add voice languages, build the pack first with `VOICE_LANGS="ITA" scripts/make_jak2_ios_data_pack.sh`.
- **Later installs:** `SKIP_DATA=1` keeps the data already on the device, and `LAUNCH=0` installs without opening the app.

On-screen DualShock 2 controls appear on iOS. They hide while a game controller is in use and come back on the next touch.

### Status and known limits

The port reaches the title screen in the Simulator and has been run on an iPhone. An audit of the iOS process in the Simulator found zero requests for executable memory.

- **Experimental:** long play sessions on iOS have not been verified. The 122 objects shared between level archives (DGOs) are the main thing still to check.
- **Graphics:** some visual defects specific to OpenGL ES may remain, such as a black triangle on a billboard, thin coloured stripes and a sky that turns cyan at times.
- **Startup:** ANGLE takes about 30 seconds to compile the shaders in the Simulator. This has not been measured on a device.
- **No live coding:** the AOT build rejects code sent from the `goalc` REPL, by design.

The decisions, test results and open issues are documented in [docs/ios/DECISIONI.md](docs/ios/DECISIONI.md) (in Italian).

## Project layout

- `goal_src/jak2/` — Jak 2 GOAL sources and build description.
- `decompiler/config/jak2/` — Jak 2 decompiler configuration.
- `game/`, `common/`, `goalc/`, `decompiler/` — shared runtime, compiler and tooling required by the Jak 2 port.
- `goalc/aot/` — emitter that turns compiled GOAL objects into signed code (`jak2-aot-emit`).
- `game/ios/`, `game/graphics/gles_compat.*`, `game/system/hid/devices/touch_controls.*` — iOS app template, OpenGL ES compatibility layer and touch controls.
- `scripts/` — iOS and AOT build, install and verification scripts.
- `test/` — compiler, runtime and Jak 2 regression tests.
- `docs/` — ARM64 implementation and QA notes. iOS notes are in `docs/ios/`.

## Credits

The OpenGL ES compatibility layer and the shader translation script are adapted from ottsel (ISC license).

OpenGOAL is a decompilation project. It does not replace the requirement to own and extract the original game legally.
