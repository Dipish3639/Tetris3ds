#include "synth.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265f
#define TABLE_BITS 11
#define TABLE_SIZE (1 << TABLE_BITS)

static float sine_table[TABLE_SIZE];
static int table_ready;

static void init_tables(void)
{
	if (table_ready)
		return;
	for (int i = 0; i < TABLE_SIZE; i++)
		sine_table[i] = sinf(2 * PI * i / TABLE_SIZE);
	table_ready = 1;
}

/* Oscillators take a phase in cycles (0..1). */
static inline float osc_sine(float ph)
{
	return sine_table[(int)(ph * TABLE_SIZE) & (TABLE_SIZE - 1)];
}

static inline float osc_tri(float ph)
{
	ph -= (int)ph;
	return ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
}

static inline float osc_pulse(float ph, float duty)
{
	ph -= (int)ph;
	return ph < duty ? 1.0f : -1.0f;
}

static uint32_t noise_state = 0x1234567u;
static inline float noise(void)
{
	noise_state = noise_state * 1664525u + 1013904223u;
	return (float)(int32_t)noise_state / 2147483648.0f;
}

static float midi_hz(int note)
{
	return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

/* Exponential decays are done by repeated multiplication: expf per sample
 * is far too slow on the 3DS CPU. */
static float decay_step(float rate)
{
	return expf(-rate / SYNTH_RATE);
}

static int16_t to_s16(float v)
{
	/* Soft clip keeps loud chords from wrapping. */
	if (v > 1.0f) v = 1.0f;
	if (v < -1.0f) v = -1.0f;
	v = v * (1.5f - 0.5f * v * v);
	return (int16_t)(v * 32000.0f);
}

/* ------------------------------------------------------------------ */
/* Music. Notes are { midi note (0 = rest), length in eighth notes }.  */

typedef struct { uint8_t note, len; } Note;

/* Korobeiniki (traditional Russian folk song, public domain). */
static const Note MELODY_A[] = {
	{76,2},{71,1},{72,1},{74,2},{72,1},{71,1},
	{69,2},{69,1},{72,1},{76,2},{74,1},{72,1},
	{71,3},{72,1},{74,2},{76,2},
	{72,2},{69,2},{69,2},{0,2},
	{0,1},{74,2},{77,1},{81,2},{79,1},{77,1},
	{76,3},{72,1},{76,2},{74,1},{72,1},
	{71,2},{71,1},{72,1},{74,2},{76,2},
	{72,2},{69,2},{69,2},{0,2},
};
static const Note MELODY_B[] = {
	{76,4},{72,4}, {74,4},{71,4}, {72,4},{69,4}, {68,4},{71,4},
	{76,4},{72,4}, {74,4},{71,4}, {72,2},{76,2},{81,4}, {80,8},
};
/* Bass root per bar. */
static const uint8_t BASS_A[8] = { 40, 45, 40, 45, 50, 48, 40, 45 };
static const uint8_t BASS_B[8] = { 45, 40, 45, 40, 45, 40, 45, 40 };

#define BATTLE_BPM 144
#define BATTLE_BARS 32 /* A A B B */

static int eighth_samples(int bpm)
{
	return SYNTH_RATE * 60 / bpm / 2;
}

static void add_melody(float *buf, int start, const Note *notes, int count, int eighth, float vol)
{
	int pos = start;
	for (int i = 0; i < count; i++) {
		int len = notes[i].len * eighth;
		if (notes[i].note) {
			float hz = midi_hz(notes[i].note);
			float ph = 0, lp = 0;
			int gate = len - eighth / 4; /* small gap between notes */
			for (int s = 0; s < len; s++) {
				float env = s < 200 ? s / 200.0f : 1.0f;
				env *= s < gate ? 1.0f - 0.25f * s / (float)len : (1.0f - (s - gate) / (float)(len - gate)) * 0.75f;
				/* Vibrato after the attack. */
				float vib = s > 4000 ? 1.0f + 0.004f * osc_sine(s / (float)SYNTH_RATE * 5.5f) : 1.0f;
				ph += hz * vib / SYNTH_RATE;
				float raw = 0.6f * osc_pulse(ph, 0.25f) + 0.4f * osc_tri(ph);
				lp += (raw - lp) * 0.35f; /* soften the square edge */
				buf[pos + s] += lp * env * vol;
			}
		}
		pos += len;
	}
}

static void add_bass_bar(float *buf, int start, int root, int eighth, float vol)
{
	for (int e = 0; e < 8; e++) {
		float hz = midi_hz(root + ((e & 1) ? 12 : 0));
		float ph = 0;
		int base = start + e * eighth;
		for (int s = 0; s < eighth; s++) {
			float env = (s < 100 ? s / 100.0f : 1.0f) * (1.0f - 0.6f * s / eighth);
			ph += hz / SYNTH_RATE;
			buf[base + s] += osc_tri(ph) * env * vol;
		}
	}
}

static void add_drums_bar(float *buf, int start, int eighth, float vol)
{
	for (int e = 0; e < 8; e++) {
		int base = start + e * eighth;
		if (e % 2 == 0) {
			/* Kick: fast falling sine. */
			float ph = 0, pitch = 1, amp = 1;
			float kp = decay_step(40.0f), ka = decay_step(18.0f);
			for (int s = 0; s < eighth; s++) {
				ph += (50.0f + 110.0f * pitch) / SYNTH_RATE;
				buf[base + s] += osc_sine(ph) * amp * vol;
				pitch *= kp;
				amp *= ka;
			}
		}
		if (e == 2 || e == 6) {
			/* Snare-ish noise burst on 2 and 4. */
			float amp = 1, k = decay_step(30.0f);
			for (int s = 0; s < eighth; s++, amp *= k)
				buf[base + s] += noise() * amp * vol * 0.45f;
		}
		/* Closed hat on every eighth. */
		float prev = 0, amp = 1, k = decay_step(90.0f);
		for (int s = 0; s < eighth / 3; s++, amp *= k) {
			float n = noise();
			buf[base + s] += (n - prev) * amp * vol * 0.18f; /* crude high-pass */
			prev = n;
		}
	}
}

/* Calm track: arpeggiated seventh chords with a bell tone and echo. */
#define CALM_BPM 84
static const uint8_t CALM_CHORDS[4][4] = {
	{ 57, 60, 64, 67 }, /* Am7   */
	{ 53, 57, 60, 64 }, /* Fmaj7 */
	{ 48, 52, 55, 59 }, /* Cmaj7 */
	{ 55, 59, 62, 64 }, /* G6    */
};
static const int8_t ARP[8] = { 0, 1, 2, 3, 2, 1, 3, 2 };

static void add_bell(float *buf, int pos, int maxPos, float hz, float vol, int len)
{
	float p1 = 0, p2 = 0, d1 = 1, d2 = 1;
	float k1 = decay_step(2.2f), k2 = decay_step(6.0f);
	for (int s = 0; s < len && pos + s < maxPos; s++) {
		p1 += hz / SYNTH_RATE;
		p2 += hz * 2.0f / SYNTH_RATE;
		float env = (s < 60 ? s / 60.0f : 1.0f) * d1;
		buf[pos + s] += (osc_sine(p1) + 0.3f * osc_sine(p2) * d2) * env * vol;
		d1 *= k1;
		d2 *= k2;
	}
}

static int track_bars(int track)
{
	return track == TRACK_BATTLE ? BATTLE_BARS : 16;
}

static int track_bpm(int track)
{
	return track == TRACK_BATTLE ? BATTLE_BPM : CALM_BPM;
}

int synth_track_len(int track)
{
	return eighth_samples(track_bpm(track)) * 8 * track_bars(track);
}

/* Shared scratch buffer, big enough for the longest track. */
static float scratch[SYNTH_RATE * 60];

void synth_track(int track, int16_t *out)
{
	init_tables();
	int len = synth_track_len(track);
	if (len > (int)(sizeof(scratch) / sizeof(scratch[0])))
		len = sizeof(scratch) / sizeof(scratch[0]);
	memset(scratch, 0, sizeof(float) * len);
	int eighth = eighth_samples(track_bpm(track));
	int bar = eighth * 8;

	if (track == TRACK_BATTLE) {
		int sec[4] = { 0, 0, 1, 1 }; /* A A B B */
		for (int part = 0; part < 4; part++) {
			int start = part * 8 * bar;
			if (sec[part] == 0)
				add_melody(scratch, start, MELODY_A, sizeof(MELODY_A) / sizeof(Note), eighth, 0.30f);
			else
				add_melody(scratch, start, MELODY_B, sizeof(MELODY_B) / sizeof(Note), eighth, 0.30f);
			for (int b = 0; b < 8; b++) {
				add_bass_bar(scratch, start + b * bar, sec[part] ? BASS_B[b] : BASS_A[b], eighth, 0.22f);
				add_drums_bar(scratch, start + b * bar, eighth, 0.35f);
			}
		}
	} else {
		for (int b = 0; b < 16; b++) {
			const uint8_t *ch = CALM_CHORDS[(b / 2) % 4];
			int start = b * bar;
			for (int e = 0; e < 8; e++) {
				int note = ch[ARP[e]] + 12;
				add_bell(scratch, start + e * eighth, len, midi_hz(note), 0.16f, eighth * 6);
			}
			/* Soft sustained bass on the chord root. */
			if (b % 2 == 0) {
				float ph = 0;
				float hz = midi_hz(ch[0] - 12);
				for (int s = 0; s < bar * 2 && start + s < len; s++) {
					float env = (s < 4000 ? s / 4000.0f : 1.0f) * (1.0f - 0.5f * s / (bar * 2));
					ph += hz / SYNTH_RATE;
					scratch[start + s] += osc_sine(ph) * env * 0.18f;
				}
			}
		}
		/* Echo that wraps around so the loop point is seamless. */
		int d = eighth * 3;
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < d; i++)
				scratch[i] += scratch[i - d + len] * 0.28f;
			for (int i = d; i < len; i++)
				scratch[i] += scratch[i - d] * 0.28f;
		}
	}

	for (int i = 0; i < len; i++)
		out[i] = to_s16(scratch[i]);
}

/* ------------------------------------------------------------------ */
/* Sound effects. */

static const float SFX_SECONDS[SFX_COUNT] = {
	[SFX_MOVE] = 0.03f, [SFX_ROTATE] = 0.05f, [SFX_HOLD] = 0.09f, [SFX_LOCK] = 0.08f,
	[SFX_HARDDROP] = 0.14f, [SFX_CLEAR1] = 0.30f, [SFX_CLEAR2] = 0.38f, [SFX_CLEAR3] = 0.45f,
	[SFX_CLEAR4] = 0.75f, [SFX_SPIN] = 0.35f, [SFX_COMBO] = 0.16f, [SFX_B2B] = 0.5f,
	[SFX_ALLCLEAR] = 1.2f, [SFX_GARBAGE] = 0.25f, [SFX_READY] = 0.15f, [SFX_GO] = 0.4f,
	[SFX_GAMEOVER] = 1.3f, [SFX_WIN] = 1.2f, [SFX_MENU_MOVE] = 0.04f, [SFX_MENU_SELECT] = 0.25f,
};

int synth_sfx_len(int sfx)
{
	return (int)(SFX_SECONDS[sfx] * SYNTH_RATE);
}

/* A plucked tone starting at `at` seconds. */
static void tone(float *buf, int len, float at, float hz, float dur, float vol, float decay, int wave)
{
	int start = (int)(at * SYNTH_RATE);
	int n = (int)(dur * SYNTH_RATE);
	float ph = 0;
	for (int s = 0; s < n && start + s < len; s++) {
		float t = s / (float)SYNTH_RATE;
		float env = (s < 40 ? s / 40.0f : 1.0f) * expf(-t * decay) * (1.0f - s / (float)n);
		ph += hz / SYNTH_RATE;
		float v = wave == 0 ? osc_sine(ph) : wave == 1 ? osc_tri(ph) : osc_pulse(ph, 0.5f) * 0.6f;
		buf[start + s] += v * env * vol;
	}
}

static void sweep(float *buf, int len, float hz0, float hz1, float vol, int wave)
{
	float ph = 0;
	for (int s = 0; s < len; s++) {
		float k = s / (float)len;
		float hz = hz0 + (hz1 - hz0) * k;
		ph += hz / SYNTH_RATE;
		float v = wave == 0 ? osc_sine(ph) : osc_pulse(ph, 0.5f) * 0.5f;
		buf[s] += v * (1.0f - k) * vol;
	}
}

static void noise_burst(float *buf, int len, float vol, float decay, float smooth)
{
	float lp = 0;
	for (int s = 0; s < len; s++) {
		lp += (noise() - lp) * smooth;
		buf[s] += lp * expf(-s / (float)SYNTH_RATE * decay) * vol;
	}
}

void synth_sfx(int sfx, int16_t *out)
{
	init_tables();
	int len = synth_sfx_len(sfx);
	float *b = scratch;
	memset(b, 0, sizeof(float) * len);

	switch (sfx) {
	case SFX_MOVE:
		tone(b, len, 0, 1400, 0.03f, 0.25f, 60, 1);
		break;
	case SFX_ROTATE:
		sweep(b, len, 900, 1500, 0.28f, 0);
		break;
	case SFX_HOLD:
		tone(b, len, 0, 660, 0.05f, 0.3f, 20, 1);
		tone(b, len, 0.04f, 990, 0.05f, 0.3f, 20, 1);
		break;
	case SFX_LOCK:
		sweep(b, len, 220, 90, 0.45f, 0);
		noise_burst(b, len, 0.12f, 80, 0.3f);
		break;
	case SFX_HARDDROP:
		sweep(b, len, 300, 60, 0.6f, 0);
		noise_burst(b, len, 0.35f, 40, 0.25f);
		break;
	case SFX_CLEAR1: case SFX_CLEAR2: case SFX_CLEAR3: case SFX_CLEAR4: {
		static const int NOTES[4] = { 76, 79, 83, 88 }; /* E G B E */
		int count = sfx - SFX_CLEAR1 + 1;
		for (int i = 0; i < count; i++)
			tone(b, len, i * 0.06f, midi_hz(NOTES[i]), SFX_SECONDS[sfx] - i * 0.06f, 0.32f, 7, 0);
		noise_burst(b, len, 0.10f + 0.04f * count, 14, 0.9f);
		if (sfx == SFX_CLEAR4)
			for (int i = 0; i < 4; i++)
				tone(b, len, 0.3f + i * 0.05f, midi_hz(NOTES[i] + 12), 0.4f, 0.16f, 6, 0);
		break;
	}
	case SFX_SPIN:
		for (int s = 0; s < len; s++) {
			float k = s / (float)len;
			b[s] += noise() * sinf(PI * k) * 0.25f;
		}
		sweep(b, len, 400, 1600, 0.25f, 0);
		break;
	case SFX_COMBO:
		tone(b, len, 0, 880, 0.16f, 0.35f, 12, 2);
		break;
	case SFX_B2B:
		for (int i = 0; i < 6; i++)
			tone(b, len, i * 0.04f, midi_hz(84 + (i % 3) * 4 + (i / 3) * 12), 0.25f, 0.18f, 10, 0);
		break;
	case SFX_ALLCLEAR: {
		static const int N[8] = { 72, 76, 79, 84, 88, 91, 96, 100 };
		for (int i = 0; i < 8; i++)
			tone(b, len, i * 0.07f, midi_hz(N[i]), 0.6f, 0.22f, 4, i & 1 ? 0 : 1);
		break;
	}
	case SFX_GARBAGE:
		for (int s = 0; s < len; s++) {
			float t = s / (float)SYNTH_RATE;
			b[s] += osc_pulse(t * 70, 0.5f) * 0.25f * (1.0f - s / (float)len);
		}
		noise_burst(b, len, 0.25f, 12, 0.08f);
		break;
	case SFX_READY:
		tone(b, len, 0, 660, 0.15f, 0.35f, 8, 2);
		break;
	case SFX_GO:
		tone(b, len, 0, 1320, 0.4f, 0.35f, 5, 2);
		tone(b, len, 0, 660, 0.4f, 0.2f, 5, 0);
		break;
	case SFX_GAMEOVER: {
		static const int N[4] = { 67, 63, 60, 55 };
		for (int i = 0; i < 4; i++)
			tone(b, len, i * 0.25f, midi_hz(N[i]), 0.5f, 0.35f, 3, 1);
		break;
	}
	case SFX_WIN: {
		static const int N[5] = { 72, 76, 79, 84, 84 };
		static const float T[5] = { 0, 0.12f, 0.24f, 0.36f, 0.6f };
		for (int i = 0; i < 5; i++)
			tone(b, len, T[i], midi_hz(N[i]), i == 4 ? 0.6f : 0.2f, 0.3f, 3, 2);
		break;
	}
	case SFX_MENU_MOVE:
		tone(b, len, 0, 1000, 0.04f, 0.25f, 40, 2);
		break;
	case SFX_MENU_SELECT:
		tone(b, len, 0, 784, 0.12f, 0.3f, 10, 2);
		tone(b, len, 0.07f, 1175, 0.18f, 0.3f, 10, 2);
		break;
	}

	for (int i = 0; i < len; i++)
		out[i] = to_s16(b[i]);
}
