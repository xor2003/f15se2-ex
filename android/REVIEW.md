# Android port: reviewer guide

This branch adds a playable Android port. It also adds mouse/touch commands to
shared input and menu code. Those shared changes are not Android-only.

For installation and controls, see [README.md](README.md). This document is a
reading guide, not a claim that every path has been verified.

## Suggested reading order

| Area | Files | Main review question |
| --- | --- | --- |
| Build and entry point | `CMakeLists.txt`, `android/app/build.gradle`, `android/app/src/main/cpp/android_main.cpp`, `.github/workflows/android.yml` | Are Android dependencies and build choices isolated from desktop? |
| Game data import | `AssetImportActivity.java`, `GameAssetInstaller.java`, `android/tests/GameAssetInstallerTest.java` | Can failure or interruption damage an existing installation or pilot roster? |
| GLES renderer | `src/r3d_gles.c`, `src/r3d_gles_compat.h`, `src/r3d_gles_compat.c`, changes to `src/r3d_gl.c` | Does the compatibility layer preserve the existing scene and drawing semantics? |
| Pointer input | Changes to `src/input.c`, `src/input.h`, `src/stpilot.c`, `src/stmissn.c`, `src/enbrief.c`, `src/egkeys.c` | Do hit regions and selection/confirmation preserve keyboard behavior? |
| Motion control | `src/android_ar.h`, `android/app/src/main/cpp/android_ar.cpp`, changes to `src/egflight.c` | When does the phone own control, and when does the game retain it? |
| Camera and lifecycle | `MainActivity.java`, `ArCameraView.java`, `AndroidManifest.xml` | Do camera permission, pause/resume, and display orientation behave safely? |
| Haptics | `android/app/src/main/cpp/android_haptics.cpp`, changes to `src/egcombat.c` | Is feedback isolated from damage calculation? |

Java filenames above are under `android/app/src/main/java/org/f15se2/ex/`.
The manifest is under `android/app/src/main/`.

## Boundaries and behavior changes

### Build and graphics

Desktop retains the desktop GL implementation. Android builds `r3d_gles.c`,
which reuses the renderer through `r3d_gles_compat` rather than duplicating scene
decoding. Platform hooks in `r3d_gl_platform.h` and `r3d_gles_platform.h`
isolate context setup, camera-view adjustment and sky transparency. Review them
with the compatibility layer: a successful compile does not establish rendering
equivalence.

AR is enabled by default in this preview. It uses the camera behind the SDL
surface and changes sky transparency. Making AR user-selectable is deferred;
the documented launch override is not an in-game settings interface.

### Input and simulation

Pointer hit testing produces game commands. Pilot and mission selection use a
first tap to select and a second tap to confirm. These menu changes also affect
desktop mouse input and deserve review independently of Android compilation.

Phone control is **not just joystick emulation**. The Android flight path can
replace pitch/roll with a requested attitude and rebuild the orientation matrix,
while heading advances from the game's yaw calculation. This was introduced to
avoid unstable feedback between the sensor target and decoded aircraft angles.
It changes how the flight model applies control and needs behavioral review,
especially around autopilot transitions, stall, ground contact, and inverted
flight. Do not treat the large `egflight.c` change as mechanical platform glue.

Android also changes initial assist/time-compression state through
`F15_ANDROID_DEFAULT_ASSIST`. Desktop builds do not define that switch.

`phoneControlsFlight()` gates all sensor flight overrides: sensors must be
ready, autopilot must be off, and look mode must be off. Camera readiness is
independent. Native angle limits are in radians; filter weights apply per
update, not per second. The cleanup preserves their existing values.

### Asset import

The APK does not bundle original game data. Gradle derives the required filename
and checksum manifest from the existing native validator. Java imports a user's
ZIP into staging, validates it, preserves the installed `HallFame`, and swaps
directories. The native loaders still read ordinary files and perform their own
validation. This does not add a new game asset format or a portable importer.

## Evidence and limits

- The desktop build and all 31 desktop tests passed for the prepared branch.
- The Android APK build passed after removing obsolete bundled-asset methods.
- Synthetic importer tests cover valid input, missing/wrong files, duplicate
  names, unsafe paths, expansion limits, roster preservation, and recovery.
- Input tests cover several cockpit hit regions, throttle mapping, view cycling,
  and debrief selection. They are not complete end-to-end menu tests.
- Device testing of the preceding playable branch is useful evidence, but does
  not establish that the new importer works across Android devices/lifecycles.
- Desktop tests do not exercise Android-only sensor, JNI, camera, or GLES paths.

## Open review items

These are unresolved, not silently accepted limitations:

1. Import work is owned by each activity instance, while staging/backup paths are
   shared. Cancellation alone does not serialize old and new activity workers.
   Installation and recovery need a single owner or explicit serialization.
2. Android CI currently runs for the named development branches/manual dispatch,
   not as a `pull_request` build gate.
3. Some control-path comments describe earlier implementations. In particular,
   touch is no longer a future feature, and the phone-controlled rotation path
   does not always call `computeAttitudeAngles()`.
4. The target-attitude controller and GLES compatibility layer need targeted
   tests beyond desktop command-mapping tests and a successful APK build.

Keep fixes to these items separate from formatting-only cleanup so reviewers can
identify behavioral changes. No branch split is required to review these areas
in order.
