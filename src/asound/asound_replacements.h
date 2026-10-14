/*
 * asound_replacements.h - Optional editable replacements for ASOUND assets.
 */
#ifndef ASOUND_REPLACEMENTS_H
#define ASOUND_REPLACEMENTS_H

#include "asound_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Recovered F15DGTL.BIN cue playback rate, shared with exported WAVs. */
#define ASND_SAMPLE_HZ 7850

typedef struct AsoundReplacementCue {
    const AsoundU8 *samples;
    int sample_count;
    int sample_rate;
} AsoundReplacementCue;

/* Reload all known replacement cues (WAV/OGG/MP3). Missing or malformed files
 * remain legacy fallbacks. The legacy blob is only used for asset comparison. */
int asound_load_replacement_cues(const AsoundU8 *legacy_blob, int legacy_size);

/* Return one past the highest legacy byte offset covered by a loaded cue. */
int asound_replacement_logical_span(void);

/* Return a loaded cue matching the original inclusive sample range. */
int asound_find_replacement_cue(AsoundU16 start, AsoundU16 end_inclusive,
                                AsoundReplacementCue *cue);

/* Read-only enumeration for diagnostics and asset validation tools. */
int asound_replacement_cue_count(void);
int asound_replacement_cue_at(int index, AsoundU16 *start,
                              AsoundU16 *end_inclusive,
                              AsoundReplacementCue *cue);

#ifdef __cplusplus
}
#endif

#endif /* ASOUND_REPLACEMENTS_H */
