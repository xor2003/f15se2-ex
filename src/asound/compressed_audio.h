#ifndef F15_COMPRESSED_AUDIO_H
#define F15_COMPRESSED_AUDIO_H

#include <stdint.h>

typedef struct CompressedAudio {
    int16_t *samples;
    uint64_t frames;
    int channels;
    int sampleRate;
} CompressedAudio;

int compressedAudioLoad(const char *path, CompressedAudio *audio);
void compressedAudioFree(CompressedAudio *audio);

#endif
