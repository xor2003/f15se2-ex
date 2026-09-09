# Android port

The APK contains no original game data. Use your own F-15 Strike Eagle II
v451.03 files (the Desert Storm expansion version supported by the native game).

## Install and play

1. Install the APK on an arm64 Android device.
2. Put a ZIP of your game directory on the phone, for example in Downloads.
3. Open the app and choose **Import game ZIP** in the system file picker.
4. After validation succeeds, tap **Play**.

A containing folder inside the ZIP is accepted. Filename case does not matter.
Import reports the first missing, damaged, or unsupported file; it does not
download game data. Android does not need an all-files storage permission.
The importer limits archives to 4096 entries and 128 MiB of expanded data.

Open the app again to play or import another ZIP. Reimport preserves the current
`HallFame` pilot roster. Other non-game files from the ZIP are not installed.
Failed validation leaves an existing installation untouched. Interrupted swaps
restore the previous installation on the next launch.

App updates signed with the same key retain game files. Uninstalling the app
removes its app-specific storage. Save export is not implemented yet.

## Controls

- Menus: first tap selects, second tap confirms.
- Phone tilt controls pitch and bank; the game controls aircraft heading.
- Autopilot ignores phone tilt. Tap above the cockpit to cycle chase, trailing,
  side and cockpit views. In an external view, any tap advances the cycle.
- Cockpit weapon pictures/numbers select weapons; the target area fires.
- The left and middle displays cycle zoom; the right display cycles targets.
- Drag the throttle gauge to change thrust. Tap the gear indicator to toggle gear.
- R releases chaff; I releases flares. The two toggles above the right display
  control autopilot (left) and free look (right).
- The soft keyboard opens only while editing a pilot name.

Camera-background mode is enabled by default. Camera permission is optional;
denial restores the game sky. This is not geographic AR or positional tracking.
Developers can launch normal rendering with:

```sh
adb shell am start -S -n org.f15se2.ex/.MainActivity --es F15_AR 0
```

## Build and tests

The Android APK GitHub Actions workflow installs Java 17, Gradle 8.10.2,
Android SDK 35, NDK 28.2.13676358 and CMake 3.22.1. It downloads the pinned,
checksum-verified SDL3 AAR, runs the importer tests, and builds the arm64 APK.
The workflow does not stage any game data. For a local build, use the same tools
and put `SDL3-3.4.10.aar` in `android/app/libs/`, then run:

```sh
sh android/test-assets.sh
gradle -p android --no-daemon assembleDebug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The build generates the importer's checksum manifest from
`src/shared/file_io.c`; the native validator remains unchanged and rechecks
the files when the game starts. Importer tests use synthetic data only.

For public updates, retain a single signing key rather than publishing each
CI runner's temporary debug signature. Never commit signing keys, APKs,
original game files, or artwork extracted from the original game.
