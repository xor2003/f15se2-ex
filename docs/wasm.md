# Browser port

This branch builds the Android TV base for browsers using Emscripten, SDL3
and the existing GLES renderer. Android camera, sensors and JNI are not built.
Keyboard, mouse and browser-supported gamepads use the existing mapping screen.

## Build

Install and activate the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html), then:

```sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j2
python3 -m http.server --directory build-wasm 8000
```

Open `http://localhost:8000`. Do not open `index.html` through `file://`.
The Browser build workflow also produces a downloadable artifact. Serve its
contents from a static HTTP server; HTTPS is recommended outside localhost.
No commercial game assets are included.

## Play and save

1. Extract your original DOS game files locally.
2. Select their folder in the launcher. It must contain `15FLT.3D3`.
3. Click Start flight. Click the canvas if keyboard input goes to the page.
4. Use Save to browser before closing the tab. Export pilot file downloads
   `HallFame`; put it in your game folder before importing to restore a backup.

Import replaces the browser's existing game folder, including its pilot file.
ZIP import is not implemented; extract archives before selecting a folder.
Folder selection needs a browser supporting directory file inputs.

Game files and pilot progress live in `/game`; SDL control preferences live in
`/libsdl`. Both are backed by IndexedDB. Autosave runs every five seconds;
closing the tab immediately after a change can lose it. Storage is specific to
the browser profile and origin (including port). Private browsing, storage
eviction and clearing site data can remove it. Pilot export does not include
control mappings. Reload the page before starting another session.

## Implementation boundaries

`web/` owns importing, persistence and launch UI. `cmake/wasm.cmake` contains
browser build settings. `web_runtime.c` yields from the shared timer pump so
legacy polling loops do not block browser events. Asyncify retains their
synchronous control flow; it is not a simulation rewrite.

WebGL uses GLES2 with Emscripten's client-side array emulation (`FULL_ES2`).
Software rendering is also selectable. Neither browser backend uses Android AR
or phone attitude controls. Browser gamepad support and audio activation depend
on browser/device permissions. This initial port still needs a browser playthrough;
native tests alone do not validate WebGL, Asyncify or IndexedDB behavior.
