#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

/* Everything the game plays is synthesised at start-up into 16-bit mono PCM. */
#define SYNTH_RATE 22050

enum {
	TRACK_BATTLE,  /* Korobeiniki, for marathon and versus */
	TRACK_CALM,    /* gentle arpeggios, for zen and the menu */
	TRACK_COUNT
};

enum {
	SFX_MOVE, SFX_ROTATE, SFX_HOLD, SFX_LOCK, SFX_HARDDROP,
	SFX_CLEAR1, SFX_CLEAR2, SFX_CLEAR3, SFX_CLEAR4,
	SFX_SPIN, SFX_COMBO, SFX_B2B, SFX_ALLCLEAR, SFX_GARBAGE,
	SFX_READY, SFX_GO, SFX_GAMEOVER, SFX_WIN,
	SFX_MENU_MOVE, SFX_MENU_SELECT,
	SFX_COUNT
};

/* Number of samples a track / effect needs. */
int synth_track_len(int track);
int synth_sfx_len(int sfx);

/* Render into `out`, which must hold the length above. */
void synth_track(int track, int16_t *out);
void synth_sfx(int sfx, int16_t *out);

#endif
