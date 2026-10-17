#ifndef AUDIO_H
#define AUDIO_H

#include <stdbool.h>
#include "synth.h"

/* Starts the DSP and synthesises every sound. Returns false when audio is
 * unavailable (usually a missing /3ds/dspfirm.cdc); the game then stays silent. */
bool audio_init(void);
void audio_exit(void);

/* Switch the looping background track (-1 = none). Same track keeps playing. */
void audio_music(int track);
/* Music volume target 0..1; changes fade smoothly. */
void audio_music_volume(float v);

void audio_sfx(int sfx);
void audio_sfx_pitch(int sfx, float pitch);

void audio_enable(bool music, bool sfx);
/* Call once per frame to advance fades. */
void audio_update(void);

#endif
