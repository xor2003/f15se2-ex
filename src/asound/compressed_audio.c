#include "compressed_audio.h"

#include <SDL3/SDL.h>

#include <stdlib.h>
#include <string.h>

#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"

#include "stb_vorbis.c"

static const char *fileExtension(const char *path) {
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot : "";
}

int compressedAudioLoad(const char *path, CompressedAudio *audio) {
    const char *extension;
    if (!audio) return 0;
    memset(audio, 0, sizeof(*audio));
    if (!path) return 0;
    extension = fileExtension(path);

    if (!SDL_strcasecmp(extension, ".mp3")) {
        drmp3_config config;
        drmp3_uint64 frames = 0;
        audio->samples = drmp3_open_file_and_read_pcm_frames_s16(path, &config, &frames, NULL);
        if (!audio->samples) return 0;
        audio->frames = frames;
        audio->channels = (int)config.channels;
        audio->sampleRate = (int)config.sampleRate;
        if (audio->frames > 0 && audio->channels > 0 && audio->sampleRate > 0) return 1;
        compressedAudioFree(audio);
        return 0;
    }

    if (!SDL_strcasecmp(extension, ".ogg")) {
        short *samples = NULL;
        int channels = 0;
        int sampleRate = 0;
        int frames = stb_vorbis_decode_filename(path, &channels, &sampleRate, &samples);
        if (frames <= 0 || !samples || channels <= 0 || sampleRate <= 0) {
            free(samples);
            return 0;
        }
        audio->samples = samples;
        audio->frames = (uint64_t)frames;
        audio->channels = channels;
        audio->sampleRate = sampleRate;
        return 1;
    }
    return 0;
}

void compressedAudioFree(CompressedAudio *audio) {
    if (!audio) return;
    free(audio->samples);
    memset(audio, 0, sizeof(*audio));
}
