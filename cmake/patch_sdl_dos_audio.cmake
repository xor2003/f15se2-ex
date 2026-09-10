# The pinned SDL DOS audio backend yields only when its ring buffer is full.
# Slow mixing can leave it permanently empty, starving the cooperative main
# thread. Keep this workaround until the SDL revision includes a fairness fix.
# Upstream: https://github.com/libsdl-org/SDL/pull/16288
# Remove this script and its PATCH_COMMAND after updating and testing SDL.
set(audio_source "${SDL_SOURCE_DIR}/src/audio/dos/SDL_dosaudio_sb.c")
file(READ "${audio_source}" source)
set(original "    const int size = hidden->ring_size;\n\n    for (;;) {")
set(replacement "    const int size = hidden->ring_size;\n\n    // Yield even if slow mixing keeps the ring from filling: the main thread\n    // must process input and advance the game between audio iterations.\n    DOS_Yield();\n\n    for (;;) {")
string(FIND "${source}" "${replacement}" patched_position)
if(patched_position EQUAL -1)
    string(FIND "${source}" "${original}" original_position)
    if(original_position EQUAL -1)
        message(FATAL_ERROR "SDL DOS audio wait changed; review the cooperative yield patch")
    endif()
    string(REPLACE "${original}" "${replacement}" source "${source}")
    file(WRITE "${audio_source}" "${source}")
endif()
