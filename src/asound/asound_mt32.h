/*
 * asound_mt32.h - Roland MT-32 backend (rsound_model + libmt32emu).
 * See asound_mt32.c for ROM discovery and runtime-selection details.
 */
#ifndef ASOUND_MT32_H
#define ASOUND_MT32_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Open the synth at `rate` Hz using discovered ROMs. 1 = ready. */
int asound_mt32_open(unsigned rate);
void asound_mt32_close(void);
int asound_mt32_is_open(void);

/* RsoundMidiSink/RsoundSysexSink implementations for rsound_model. */
void asound_mt32_midi(void *user, uint32_t msg);
void asound_mt32_sysex(void *user, const uint8_t *frame, size_t len);

/* Render `frames` stereo S16 samples into buf (2*frames ints). Silent when
 * the synth is not open. */
void asound_mt32_render(int16_t *buf, int frames);

#ifdef __cplusplus
}
#endif
#endif /* ASOUND_MT32_H */
