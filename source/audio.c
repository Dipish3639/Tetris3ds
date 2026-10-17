#include <3ds.h>
#include <string.h>
#include "audio.h"

#define MUSIC_CH     0
#define SFX_CH_FIRST 1
#define SFX_CH_COUNT 12
#define MUSIC_GAIN   0.55f

static bool ready;
static bool dspStarted;
static bool musicOn = true, sfxOn = true;

static s16 *trackData[TRACK_COUNT];
static int trackLen[TRACK_COUNT];
static s16 *sfxData[SFX_COUNT];
static int sfxLen[SFX_COUNT];

static ndspWaveBuf musicBuf;
static ndspWaveBuf sfxBuf[SFX_CH_COUNT];
static int nextSfx;
static int curTrack = -1;
static float musicVol, musicTarget = 1.0f;

static void set_volume(int ch, float v)
{
	float mix[12] = { 0 };
	mix[0] = mix[1] = v;
	ndspChnSetMix(ch, mix);
}

static void setup_channel(int ch)
{
	ndspChnReset(ch);
	ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
	ndspChnSetRate(ch, SYNTH_RATE);
	ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
	set_volume(ch, 1.0f);
}

bool audio_init(void)
{
	if (R_FAILED(ndspInit()))
		return false;
	dspStarted = true;
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);

	for (int t = 0; t < TRACK_COUNT; t++) {
		trackLen[t] = synth_track_len(t);
		trackData[t] = linearAlloc(trackLen[t] * sizeof(s16));
		if (!trackData[t])
			goto fail;
		synth_track(t, trackData[t]);
		DSP_FlushDataCache(trackData[t], trackLen[t] * sizeof(s16));
	}
	for (int s = 0; s < SFX_COUNT; s++) {
		sfxLen[s] = synth_sfx_len(s);
		sfxData[s] = linearAlloc(sfxLen[s] * sizeof(s16));
		if (!sfxData[s])
			goto fail;
		synth_sfx(s, sfxData[s]);
		DSP_FlushDataCache(sfxData[s], sfxLen[s] * sizeof(s16));
	}

	setup_channel(MUSIC_CH);
	for (int i = 0; i < SFX_CH_COUNT; i++)
		setup_channel(SFX_CH_FIRST + i);
	ready = true;
	return true;

fail:
	audio_exit();
	return false;
}

void audio_exit(void)
{
	if (ready) {
		ndspChnWaveBufClear(MUSIC_CH);
		for (int i = 0; i < SFX_CH_COUNT; i++)
			ndspChnWaveBufClear(SFX_CH_FIRST + i);
	}
	for (int t = 0; t < TRACK_COUNT; t++) {
		if (trackData[t])
			linearFree(trackData[t]);
		trackData[t] = NULL;
	}
	for (int s = 0; s < SFX_COUNT; s++) {
		if (sfxData[s])
			linearFree(sfxData[s]);
		sfxData[s] = NULL;
	}
	if (dspStarted)
		ndspExit();
	dspStarted = false;
	ready = false;
}

void audio_music(int track)
{
	if (!ready || track == curTrack)
		return;
	ndspChnWaveBufClear(MUSIC_CH);
	curTrack = track;
	if (track < 0)
		return;
	memset(&musicBuf, 0, sizeof(musicBuf));
	musicBuf.data_vaddr = trackData[track];
	musicBuf.nsamples = trackLen[track];
	musicBuf.looping = true;
	musicVol = 0; /* fade in */
	set_volume(MUSIC_CH, 0);
	ndspChnWaveBufAdd(MUSIC_CH, &musicBuf);
}

void audio_music_volume(float v)
{
	musicTarget = v;
}

void audio_update(void)
{
	if (!ready)
		return;
	float target = musicOn ? musicTarget * MUSIC_GAIN : 0.0f;
	if (musicVol != target) {
		float step = 0.02f;
		if (musicVol < target)
			musicVol = musicVol + step > target ? target : musicVol + step;
		else
			musicVol = musicVol - step < target ? target : musicVol - step;
		set_volume(MUSIC_CH, musicVol);
	}
}

void audio_sfx_pitch(int sfx, float pitch)
{
	if (!ready || !sfxOn || sfx < 0 || sfx >= SFX_COUNT)
		return;

	/* Prefer an idle channel; otherwise steal the oldest one. */
	int pick = -1;
	for (int i = 0; i < SFX_CH_COUNT; i++) {
		int c = (nextSfx + i) % SFX_CH_COUNT;
		u8 st = sfxBuf[c].status;
		if (st == NDSP_WBUF_FREE || st == NDSP_WBUF_DONE) {
			pick = c;
			break;
		}
	}
	if (pick < 0)
		pick = nextSfx;
	nextSfx = (pick + 1) % SFX_CH_COUNT;

	int ch = SFX_CH_FIRST + pick;
	ndspChnWaveBufClear(ch);
	ndspChnSetRate(ch, SYNTH_RATE * pitch);
	memset(&sfxBuf[pick], 0, sizeof(ndspWaveBuf));
	sfxBuf[pick].data_vaddr = sfxData[sfx];
	sfxBuf[pick].nsamples = sfxLen[sfx];
	ndspChnWaveBufAdd(ch, &sfxBuf[pick]);
}

void audio_sfx(int sfx)
{
	audio_sfx_pitch(sfx, 1.0f);
}

void audio_enable(bool music, bool sfx)
{
	musicOn = music;
	sfxOn = sfx;
}
