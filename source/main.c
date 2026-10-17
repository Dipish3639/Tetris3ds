#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "audio.h"
#include "net.h"
#include "render.h"

/* ------------------------------------------------------------------ */
/* Versus protocol. Every packet is a full snapshot, so lost packets   */
/* don't matter: garbage is sent as a running total per round.         */

#define NET_MAGIC 0x5434

enum { ST_PLAYING, ST_DEAD, ST_WON, ST_READY };

typedef struct __attribute__((packed)) {
	u16 magic;
	u8  round;
	u8  status;
	u16 garbageSent;
	u16 lines;
	u32 score;
	u8  cells[VISIBLE_H][BOARD_W];
} NetState;

#define PEER_TIMEOUT_FRAMES (60 * 6)
#define COUNTDOWN_FRAMES    100 /* READY for 70 frames, then GO while play starts */

/* ------------------------------------------------------------------ */

typedef enum { MODE_MENU, MODE_HOST_WAIT, MODE_JOIN_SCAN, MODE_SOLO, MODE_VERSUS } Mode;

static Mode mode;
static Game game;
static UiState ui;
static bool zenMode;
static char message[48];

/* Versus state. */
static u8 myRound;
static u8 myStatus;
static int garbageReceived;
static NetState opp;
static bool haveOpp;
static int framesSinceRecv;

/* Effects bookkeeping. */
static int seenClearEvent, seenLockEvent;

/* Sound bookkeeping: last seen value of each game event counter. */
static struct {
	int move, rotate, hold, hardDrop, lock, clear, garbage;
} heard;
static bool audioOk, musicOn = true, sfxOn = true;

static u32 seed(void)
{
	return (u32)osGetTime() ^ (u32)svcGetSystemTick();
}

static void reset_effects(void)
{
	seenClearEvent = game.clearEvent;
	seenLockEvent = game.lockEvent;
	ui.flashTimer = 0;
	ui.actionTimer = 0;
	ui.actionAllClear = false;
	ui.countdown = COUNTDOWN_FRAMES;
	heard.move = game.moveEvent;
	heard.rotate = game.rotateEvent;
	heard.hold = game.holdEvent;
	heard.hardDrop = game.hardDropEvent;
	heard.lock = game.lockEvent;
	heard.clear = game.clearEvent;
	heard.garbage = game.garbageEvent;
}

/* Play sounds for whatever the game did this frame. */
static void game_sounds(void)
{
	if (game.moveEvent != heard.move)
		audio_sfx(SFX_MOVE);
	if (game.rotateEvent != heard.rotate)
		audio_sfx(SFX_ROTATE);
	if (game.holdEvent != heard.hold)
		audio_sfx(SFX_HOLD);

	bool hard = game.hardDropEvent != heard.hardDrop;
	bool locked = game.lockEvent != heard.lock;
	if (hard)
		audio_sfx(SFX_HARDDROP);
	else if (locked && game.lastClear == 0)
		audio_sfx(SFX_LOCK);

	if (game.clearEvent != heard.clear) {
		if (game.lastSpin != SPIN_NONE)
			audio_sfx(SFX_SPIN);
		if (game.lastClear > 0) {
			int n = game.lastClear > 4 ? 4 : game.lastClear;
			audio_sfx(SFX_CLEAR1 + n - 1);
			if (game.combo >= 1) {
				/* Each combo step is a semitone higher. */
				int step = game.combo > 16 ? 16 : game.combo;
				float pitch = 1.0f;
				for (int i = 0; i < step; i++)
					pitch *= 1.05946f;
				audio_sfx_pitch(SFX_COMBO, pitch);
			}
			if (game.b2b > 0)
				audio_sfx(SFX_B2B);
			if (game.lastAllClear)
				audio_sfx(SFX_ALLCLEAR);
		}
	}
	if (game.garbageEvent != heard.garbage)
		audio_sfx(SFX_GARBAGE);

	heard.move = game.moveEvent;
	heard.rotate = game.rotateEvent;
	heard.hold = game.holdEvent;
	heard.hardDrop = game.hardDropEvent;
	heard.lock = game.lockEvent;
	heard.clear = game.clearEvent;
	heard.garbage = game.garbageEvent;
}

static void start_solo(void)
{
	game_init(&game, seed());
	game.zen = zenMode;
	reset_effects();
}

static void start_round(void)
{
	game_init(&game, seed());
	myStatus = ST_PLAYING;
	garbageReceived = 0;
	reset_effects();
}

static void start_versus(void)
{
	mode = MODE_VERSUS;
	myRound = 0;
	ui.wins = ui.losses = 0;
	haveOpp = false;
	framesSinceRecv = 0;
	memset(&opp, 0, sizeof(opp));
	start_round();
}

static void back_to_menu(const char *msg)
{
	net_close();
	mode = MODE_MENU;
	ui.paused = false;
	snprintf(message, sizeof(message), "%s", msg ? msg : "");
}

/* Turn the result of the latest lock into TETR.IO-style popups. */
static void update_effects(void)
{
	if (game.lockEvent != seenLockEvent) {
		seenLockEvent = game.lockEvent;
		if (game.lastClear > 0) {
			ui.flashTimer = 14;
			ui.flashRows = game.clearedRows;
		}
	}
	if (game.clearEvent != seenClearEvent) {
		seenClearEvent = game.clearEvent;
		static const char *NAMES[5] = { "", "SINGLE", "DOUBLE", "TRIPLE", "QUAD" };
		bool spin = game.lastSpin != SPIN_NONE;
		bool comboing = game.lastClear > 0 && game.combo >= 1;
		if (game.lastClear == 1 && !spin && !comboing && !game.lastAllClear)
			goto tick;

		ui.actionTop[0] = ui.actionMain[0] = ui.actionSub[0] = 0;
		ui.actionColor = 0xC24BD8;
		if (spin)
			snprintf(ui.actionTop, sizeof(ui.actionTop), game.lastSpin == SPIN_MINI ? "T-SPIN MINI" : "T-SPIN");
		else if (game.lastClear > 0 && game.b2b > 0) {
			snprintf(ui.actionTop, sizeof(ui.actionTop), "B2B X%d", game.b2b);
			ui.actionColor = 0xFFD54A;
		}
		snprintf(ui.actionMain, sizeof(ui.actionMain), "%s", NAMES[game.lastClear > 4 ? 4 : game.lastClear]);
		if (comboing && spin && game.b2b > 0)
			snprintf(ui.actionSub, sizeof(ui.actionSub), "%d COMBO B2B X%d", game.combo, game.b2b);
		else if (comboing)
			snprintf(ui.actionSub, sizeof(ui.actionSub), "%d COMBO", game.combo);
		else if (spin && game.b2b > 0)
			snprintf(ui.actionSub, sizeof(ui.actionSub), "B2B X%d", game.b2b);
		ui.actionAllClear = game.lastAllClear;
		ui.actionTimer = 90;
	}
tick:
	if (ui.flashTimer > 0)
		ui.flashTimer--;
	if (ui.actionTimer > 0 && --ui.actionTimer == 0)
		ui.actionAllClear = false;
}

static void send_state(void)
{
	NetState s;
	s.magic = NET_MAGIC;
	s.round = myRound;
	s.status = myStatus;
	s.garbageSent = (u16)game.garbageOut;
	s.lines = (u16)game.lines;
	s.score = game.score;
	memcpy(s.cells, &game.cells[HIDDEN_H][0], sizeof(s.cells));
	if (!game.over)
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				int br = game.y + r - HIDDEN_H;
				if (piece_cell(game.type, game.rot, r, c) && br >= 0 && br < VISIBLE_H)
					s.cells[br][game.x + c] = (u8)(game.type + 1);
			}
	net_send(&s, sizeof(s));
}

static void poll_network(int steps)
{
	NetState in;
	size_t n;
	framesSinceRecv += steps;
	while ((n = net_recv(&in, sizeof(in))) > 0) {
		if (n != sizeof(in) || in.magic != NET_MAGIC)
			continue;
		opp = in;
		haveOpp = true;
		framesSinceRecv = 0;
	}
	if (!haveOpp)
		return;

	if (opp.round == myRound) {
		/* New garbage from the opponent. */
		if (opp.garbageSent > garbageReceived) {
			game_add_garbage(&game, opp.garbageSent - garbageReceived);
			garbageReceived = opp.garbageSent;
		}
		/* Opponent topped out while we're still alive: we win. */
		if (myStatus == ST_PLAYING && (opp.status == ST_DEAD || opp.status == ST_READY)) {
			myStatus = ST_WON;
			ui.wins++;
			audio_sfx(SFX_WIN);
		}
	}

	/* Rematch once both players are ready (or the other side already moved on). */
	if (myStatus == ST_READY &&
	    ((opp.round == myRound && opp.status == ST_READY) || opp.round == (u8)(myRound + 1))) {
		myRound++;
		start_round();
	}
}

static void map_input(u32 down, u32 held, GameInput *in)
{
	in->held = 0;
	in->pressed = 0;
	if (held & (KEY_DLEFT | KEY_CPAD_LEFT))   in->held |= IN_LEFT;
	if (held & (KEY_DRIGHT | KEY_CPAD_RIGHT)) in->held |= IN_RIGHT;
	if (held & (KEY_DDOWN | KEY_CPAD_DOWN))   in->held |= IN_DOWN;
	if (down & (KEY_DUP | KEY_CPAD_UP))       in->pressed |= IN_HARD;
	if (down & (KEY_A | KEY_X))               in->pressed |= IN_ROT_CW;
	if (down & (KEY_B | KEY_Y))               in->pressed |= IN_ROT_CCW;
	if (down & (KEY_L | KEY_R))               in->pressed |= IN_HOLD;
}

/* Run one frame of play; countdown first, then the game itself. */
static void play_frame(const GameInput *in)
{
	if (ui.countdown == COUNTDOWN_FRAMES)
		audio_sfx(SFX_READY);
	if (ui.countdown > 0 && --ui.countdown == 30)
		audio_sfx(SFX_GO);
	if (ui.countdown <= 30) {
		bool wasOver = game.over;
		game_update(&game, in);
		game_sounds();
		if (game.over && !wasOver)
			audio_sfx(SFX_GAMEOVER);
	}
	update_effects();
}

/* Run as many 60 Hz game steps as real time calls for. Button presses only
 * count for the first step so they aren't applied twice. */
static void run_play(const GameInput *in, int steps)
{
	GameInput step = *in;
	for (int i = 0; i < steps && !game.over; i++) {
		play_frame(&step);
		step.pressed = 0;
	}
}

/* Pick the background track and its volume for the current screen. */
static void update_music(void)
{
	int track = TRACK_CALM;
	float vol = 0.8f;
	if (mode == MODE_SOLO || mode == MODE_VERSUS) {
		track = (mode == MODE_SOLO && zenMode) ? TRACK_CALM : TRACK_BATTLE;
		vol = 1.0f;
		bool finished = (mode == MODE_SOLO && game.over) || (mode == MODE_VERSUS && myStatus != ST_PLAYING);
		if (ui.paused || finished)
			vol = 0.35f;
	}
	audio_music(track);
	audio_music_volume(vol);
	audio_update();
}

static void menu_select(int item, bool *netOk)
{
	message[0] = 0;
	audio_sfx(SFX_MENU_SELECT);
	switch (item) {
	case MENU_MARATHON:
	case MENU_ZEN:
		zenMode = item == MENU_ZEN;
		mode = MODE_SOLO;
		ui.paused = false;
		start_solo();
		break;
	case MENU_HOST:
	case MENU_JOIN:
		if (!*netOk && !(*netOk = net_init())) {
			snprintf(message, sizeof(message), "LOCAL WIRELESS UNAVAILABLE");
		} else if (item == MENU_HOST) {
			if (net_host_start())
				mode = MODE_HOST_WAIT;
			else
				snprintf(message, sizeof(message), "COULD NOT CREATE ROOM");
		} else {
			mode = MODE_JOIN_SCAN;
		}
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Cached backgrounds, stored in framebuffer layout so a memcpy restores them. */

static u8 topBg[TOP_W * TOP_H * 3];
static u8 botBg[BOT_W * BOT_H * 3];

static void build_backgrounds(void)
{
	Canvas cv;
	cv_init_rotated(&cv, topBg, TOP_W, TOP_H);
	render_background(&cv, true);
	cv_init_rotated(&cv, botBg, BOT_W, BOT_H);
	render_background(&cv, false);
}

static void fill_ui(void)
{
	ui.message = message;
	ui.audioOk = audioOk;
	ui.musicOn = musicOn;
	ui.sfxOn = sfxOn;
	ui.game = &game;
	ui.zen = zenMode;
	ui.versus = mode == MODE_VERSUS;
	switch (mode) {
	case MODE_MENU:      ui.screen = SCR_MENU; break;
	case MODE_HOST_WAIT: ui.screen = SCR_HOST_WAIT; break;
	case MODE_JOIN_SCAN: ui.screen = SCR_JOIN_SCAN; break;
	default:             ui.screen = SCR_PLAY; break;
	}

	ui.result = RES_NONE;
	if (mode == MODE_SOLO && game.over)
		ui.result = RES_GAMEOVER;
	if (mode == MODE_VERSUS) {
		switch (myStatus) {
		case ST_DEAD:  ui.result = RES_LOSE; ui.lastWin = false; break;
		case ST_WON:   ui.result = RES_WIN;  ui.lastWin = true; break;
		case ST_READY: ui.result = RES_READY; break;
		}
		ui.haveOpp = haveOpp;
		ui.oppCells = &opp.cells[0][0];
		ui.oppDead = haveOpp && opp.round == myRound && opp.status == ST_DEAD;
		ui.oppScore = opp.score;
		ui.oppLines = opp.lines;
		ui.oppReady = haveOpp && opp.status == ST_READY;
	}
}

int main(void)
{
	gfxInitDefault();
	osSetSpeedupEnable(true); /* 804 MHz on New 3DS */
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, true);
	build_backgrounds();

	/* Show a loading screen while the music is synthesised. */
	for (int i = 0; i < 2; i++) {
		Canvas cv;
		ui.screen = SCR_LOADING;
		u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		memcpy(fb, topBg, sizeof(topBg));
		cv_init_rotated(&cv, fb, TOP_W, TOP_H);
		render_top(&cv, &ui);
		fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
		memcpy(fb, botBg, sizeof(botBg));
		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}
	audioOk = audio_init();

	bool netOk = net_init();
	mode = MODE_MENU;
	int scanCooldown = 0;

	/* Game logic runs on a fixed 60 Hz clock, independent of how long a
	 * frame takes to draw, so speed and the timer always match real time. */
	u64 lastTick = svcGetSystemTick();
	u64 tickAcc = 0;
	const u64 TICKS_PER_STEP = SYSCLOCK_ARM11 / 60;

	while (aptMainLoop()) {
		u64 now = svcGetSystemTick();
		tickAcc += now - lastTick;
		lastTick = now;
		int steps = (int)(tickAcc / TICKS_PER_STEP);
		tickAcc -= (u64)steps * TICKS_PER_STEP;
		if (steps > 4) /* after a long stall (e.g. HOME menu), don't fast-forward */
			steps = 4;

		hidScanInput();
		u32 down = hidKeysDown();
		u32 held = hidKeysHeld();
		GameInput in;
		map_input(down, held, &in);

		switch (mode) {
		case MODE_MENU:
			if (down & KEY_START)
				goto quit;
			if (down & (KEY_DUP | KEY_CPAD_UP)) {
				ui.menuSel = (ui.menuSel + MENU_COUNT - 1) % MENU_COUNT;
				audio_sfx(SFX_MENU_MOVE);
			}
			if (down & (KEY_DDOWN | KEY_CPAD_DOWN)) {
				ui.menuSel = (ui.menuSel + 1) % MENU_COUNT;
				audio_sfx(SFX_MENU_MOVE);
			}
			if (down & KEY_X)
				musicOn = !musicOn;
			if (down & KEY_Y) {
				sfxOn = !sfxOn;
				audio_enable(musicOn, sfxOn);
				audio_sfx(SFX_MENU_MOVE);
			}
			audio_enable(musicOn, sfxOn);
			if (down & KEY_TOUCH) {
				touchPosition t;
				hidTouchRead(&t);
				int hit = menu_hit(t.px, t.py);
				if (hit >= 0) {
					ui.menuSel = hit;
					menu_select(hit, &netOk);
				}
			} else if (down & KEY_A) {
				menu_select(ui.menuSel, &netOk);
			}
			if (mode == MODE_JOIN_SCAN)
				scanCooldown = 0;
			break;

		case MODE_HOST_WAIT:
			if (down & (KEY_B | KEY_TOUCH)) {
				audio_sfx(SFX_MENU_MOVE);
				back_to_menu("");
			}
			else if (net_peer_connected())
				start_versus();
			break;

		case MODE_JOIN_SCAN:
			if (down & (KEY_B | KEY_TOUCH)) {
				audio_sfx(SFX_MENU_MOVE);
				back_to_menu("");
			} else if (--scanCooldown <= 0) {
				scanCooldown = 60;
				int r = net_join_try();
				if (r > 0)
					start_versus();
				else if (r < 0)
					back_to_menu("SCAN FAILED");
			}
			break;

		case MODE_SOLO:
			if (game.over) {
				for (int i = 0; i < steps; i++)
					update_effects();
				if (down & KEY_A)
					start_solo();
				else if (down & KEY_START)
					back_to_menu("");
			} else if (ui.paused) {
				if (down & KEY_START)
					ui.paused = false;
				else if (down & KEY_SELECT)
					back_to_menu("");
			} else if (down & KEY_START) {
				ui.paused = true;
			} else {
				run_play(&in, steps);
			}
			break;

		case MODE_VERSUS:
			poll_network(steps);
			if (ui.paused) {
				/* The match keeps running; this is only a leave prompt. */
				if (down & KEY_A) {
					back_to_menu("LEFT THE MATCH");
					break;
				}
				if (down & (KEY_B | KEY_START))
					ui.paused = false;
				in.pressed = 0;
				in.held = 0;
			} else if (down & KEY_START) {
				ui.paused = true;
			}

			if (myStatus == ST_PLAYING) {
				run_play(&in, steps);
				if (game.over) {
					myStatus = ST_DEAD;
					ui.losses++;
				}
			} else {
				for (int i = 0; i < steps; i++)
					update_effects();
				if ((myStatus == ST_DEAD || myStatus == ST_WON) && !ui.paused && (down & KEY_A))
					myStatus = ST_READY;
			}

			if (ui.frame % 3 == 0)
				send_state();

			if (framesSinceRecv > PEER_TIMEOUT_FRAMES || (haveOpp && !net_peer_connected()))
				back_to_menu("CONNECTION LOST");
			break;
		}

		update_music();
		fill_ui();
		Canvas cv;
		u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		memcpy(fb, topBg, sizeof(topBg));
		cv_init_rotated(&cv, fb, TOP_W, TOP_H);
		render_top(&cv, &ui);

		/* The bottom screen only shows stats, so redraw it every other frame
		 * (or right away when the screen changes) to save time. */
		static int lastBottomScreen = -1;
		bool drawBottom = (ui.frame & 1) == 0 || ui.screen != lastBottomScreen;
		if (drawBottom) {
			lastBottomScreen = ui.screen;
			fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
			memcpy(fb, botBg, sizeof(botBg));
			cv_init_rotated(&cv, fb, BOT_W, BOT_H);
			render_bottom(&cv, &ui);
		}

		gfxFlushBuffers();
		gfxScreenSwapBuffers(GFX_TOP, false);
		if (drawBottom)
			gfxScreenSwapBuffers(GFX_BOTTOM, false);
		gspWaitForVBlank();
		ui.frame++;
	}

quit:
	audio_exit();
	net_exit();
	gfxExit();
	return 0;
}
