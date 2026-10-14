/*
 * asound_sf2.h - SoundFont backend: rsound_model MIDI -> FluidSynth.
 *
 * Free, ROM-less middle tier between MT-32 (authentic, needs ROMs) and
 * OPL/AdLib. Plays the same decoded RSOUND stream through a GM/GS
 * SoundFont with an MT-32->GM program remap.
 */

#ifndef ASOUND_SF2_H
#define ASOUND_SF2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  asound_sf2_open(unsigned rate);
void asound_sf2_close(void);
int  asound_sf2_is_open(void);
void asound_sf2_midi(void *user, uint32_t msg);   /* packed status|d1|d2<<8 */
void asound_sf2_sysex(void *user, const uint8_t *frame, size_t len);
void asound_sf2_render(int16_t *buf, int frames); /* interleaved stereo s16 */

#ifdef __cplusplus
}
#endif

#endif
