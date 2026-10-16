#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "net.h"

/* ------------------------------------------------------------------ */
/* Top screen framebuffer drawing (400x240, BGR8, rotated 90 degrees). */

#define TOP_W 400
#define TOP_H 240

static u8 *fb;

static void fill_rect(int x, int y, int w, int h, u32 rgb)
{
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > TOP_W) w = TOP_W - x;
	if (y + h > TOP_H) h = TOP_H - y;
	if (w <= 0 || h <= 0)
		return;
	u8 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	for (int i = 0; i < w; i++) {
		/* Each screen column is a contiguous run of 240 pixels, bottom to top. */
		u8 *p = fb + ((x + i) * TOP_H + (TOP_H - 1 - (y + h - 1))) * 3;
		for (int j = 0; j < h; j++) {
			p[0] = b; p[1] = g; p[2] = r;
			p += 3;
		}
	}
}

static void frame_rect(int x, int y, int w, int h, u32 rgb)
{
	fill_rect(x, y, w, 1, rgb);
	fill_rect(x, y + h - 1, w, 1, rgb);
	fill_rect(x, y, 1, h, rgb);
	fill_rect(x + w - 1, y, 1, h, rgb);
}

static const u32 COLORS[9] = {
	0x000000,
	0x00E0E0, /* I */
	0x2040F0, /* J */
	0xF09000, /* L */
	0xF0E000, /* O */
	0x30D030, /* S */
	0xA030E0, /* T */
	0xE02020, /* Z */
	0x707070, /* garbage */
};

static u32 dim(u32 c)
{
	return ((c >> 2) & 0x3F3F3F);
}

static void draw_cell(int x, int y, int size, u8 v)
{
	u32 c = COLORS[v];
	fill_rect(x, y, size - 1, size - 1, c);
	if (size >= 6)
		fill_rect(x + 1, y + 1, size - 3, 2, ((c >> 1) & 0x7F7F7F) + 0x404040);
}

static void draw_piece_preview(int x, int y, int size, int type, bool greyed)
{
	if (type < 0)
		return;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (piece_cell(type, 0, r, c)) {
				if (greyed)
					fill_rect(x + c * size, y + r * size, size - 1, size - 1, 0x383838);
				else
					draw_cell(x + c * size, y + r * size, size, (u8)(type + 1));
			}
}

/* Main board geometry. */
#define CELL   11
#define BX     ((TOP_W - BOARD_W * CELL) / 2 - 30)
#define BY     ((TOP_H - VISIBLE_H * CELL) / 2)

/* Opponent mini board. */
#define OCELL  5
#define OX     (TOP_W - BOARD_W * OCELL - 14)
#define OY     (TOP_H - VISIBLE_H * OCELL - 12)

static void draw_board_bg(int x, int y, int cell)
{
	fill_rect(x - 2, y - 2, BOARD_W * cell + 4, VISIBLE_H * cell + 4, 0x505868);
	fill_rect(x, y, BOARD_W * cell, VISIBLE_H * cell, 0x101018);
	if (cell >= 8)
		for (int c = 1; c < BOARD_W; c++)
			fill_rect(x + c * cell - 1, y, 1, VISIBLE_H * cell, 0x181822);
}

static void draw_game(const Game *g)
{
	draw_board_bg(BX, BY, CELL);

	for (int r = 0; r < VISIBLE_H; r++)
		for (int c = 0; c < BOARD_W; c++) {
			u8 v = g->cells[r + HIDDEN_H][c];
			if (v)
				draw_cell(BX + c * CELL, BY + r * CELL, CELL, v);
		}

	if (!g->over) {
		int gy = game_ghost_y(g);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				if (!piece_cell(g->type, g->rot, r, c))
					continue;
				int sx = BX + (g->x + c) * CELL;
				int gr = gy + r - HIDDEN_H;
				if (gr >= 0)
					frame_rect(sx, BY + gr * CELL, CELL - 1, CELL - 1, dim(COLORS[g->type + 1]) * 2);
				int pr = g->y + r - HIDDEN_H;
				if (pr >= 0)
					draw_cell(sx, BY + pr * CELL, CELL, (u8)(g->type + 1));
			}
	}

	/* Incoming garbage meter left of the board. */
	fill_rect(BX - 8, BY, 4, VISIBLE_H * CELL, 0x202028);
	int pend = g->pendingGarbage > VISIBLE_H ? VISIBLE_H : g->pendingGarbage;
	if (pend > 0)
		fill_rect(BX - 8, BY + (VISIBLE_H - pend) * CELL, 4, pend * CELL, 0xF03030);

	/* Hold box. */
	int hx = BX - 60, hy = BY;
	frame_rect(hx - 3, hy - 3, 4 * 9 + 6, 2 * 9 + 8, 0x505868);
	draw_piece_preview(hx, hy, 9, g->hold, g->holdUsed);

	/* Next queue. */
	int nx = BX + BOARD_W * CELL + 12, ny = BY;
	for (int i = 0; i < NEXT_COUNT; i++) {
		int s = i == 0 ? 9 : 7;
		draw_piece_preview(nx, ny, s, g->queue[i], false);
		ny += s * 2 + 10;
	}
}

static void draw_opponent(const u8 cells[VISIBLE_H][BOARD_W], bool dead)
{
	draw_board_bg(OX, OY, OCELL);
	for (int r = 0; r < VISIBLE_H; r++)
		for (int c = 0; c < BOARD_W; c++) {
			u8 v = cells[r][c];
			if (v > CELL_GARBAGE)
				v = CELL_GARBAGE;
			if (v)
				fill_rect(OX + c * OCELL, OY + r * OCELL, OCELL - 1, OCELL - 1,
				          dead ? dim(COLORS[v]) : COLORS[v]);
		}
}

/* ------------------------------------------------------------------ */
/* Versus protocol. Every packet is a full snapshot, so lost packets   */
/* don't matter: garbage is sent as a running total per round.         */

#define NET_MAGIC 0x5433

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

/* ------------------------------------------------------------------ */

typedef enum { MODE_MENU, MODE_HOST_WAIT, MODE_JOIN_SCAN, MODE_SOLO, MODE_VERSUS } Mode;

static Mode mode;
static Game game;
static bool paused;
static char message[64];

/* Versus state. */
static u8 myRound;
static u8 myStatus;
static int garbageReceived;
static NetState opp;
static bool haveOpp;
static int framesSinceRecv;
static int wins, losses;

static u32 seed(void)
{
	return (u32)osGetTime() ^ (u32)svcGetSystemTick();
}

static bool zenMode;

static void start_solo(void)
{
	game_init(&game, seed());
	game.zen = zenMode;
}

static void start_round(void)
{
	game_init(&game, seed());
	myStatus = ST_PLAYING;
	garbageReceived = 0;
}

static void start_versus(void)
{
	mode = MODE_VERSUS;
	myRound = 0;
	wins = losses = 0;
	haveOpp = false;
	framesSinceRecv = 0;
	memset(&opp, 0, sizeof(opp));
	start_round();
	consoleClear();
}

static void back_to_menu(const char *msg)
{
	net_close();
	mode = MODE_MENU;
	paused = false;
	snprintf(message, sizeof(message), "%s", msg ? msg : "");
	consoleClear();
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

static void poll_network(void)
{
	NetState in;
	size_t n;
	framesSinceRecv++;
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
			wins++;
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

/* ------------------------------------------------------------------ */
/* Bottom screen text. */

static void print_line(int row, const char *fmt, ...)
{
	char buf[40];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("\x1b[%d;1H%-39s", row, buf);
}

static void draw_bottom(int frame)
{
	if (frame % 4)
		return;

	switch (mode) {
	case MODE_MENU:
		print_line(2,  "          TETRIS 3DS");
		print_line(5,  "  A  - Solo marathon");
		print_line(6,  "  B  - Zen (speed never increases)");
		print_line(7,  "  X  - Host versus (local wireless)");
		print_line(9,  "  Y  - Join versus (local wireless)");
		print_line(11, "  START - Exit");
		print_line(14, "  Controls:");
		print_line(15, "   D-Pad/Circle  move, Down soft drop");
		print_line(16, "   Up            hard drop");
		print_line(17, "   A/X  rotate right   B/Y  rotate left");
		print_line(18, "   L/R  hold           START pause/menu");
		print_line(21, "  Clearing 2/3/4 lines sends 1/2/4");
		print_line(22, "  garbage lines to your opponent.");
		print_line(26, "  %s", message);
		break;

	case MODE_HOST_WAIT:
		print_line(2, "  Hosting a versus game...");
		print_line(4, "  On the other 3DS choose 'Join'.");
		print_line(6, "  Waiting for player %s", (frame / 30) % 2 ? "..." : "   ");
		print_line(9, "  B - Cancel");
		break;

	case MODE_JOIN_SCAN:
		print_line(2, "  Looking for a host...");
		print_line(4, "  On the other 3DS choose 'Host'.");
		print_line(6, "  Scanning %s", (frame / 30) % 2 ? "..." : "   ");
		print_line(9, "  B - Cancel");
		break;

	case MODE_SOLO:
		print_line(2,  zenMode ? "  ZEN" : "  SOLO");
		print_line(4,  "  Score : %lu", (unsigned long)game.score);
		print_line(5,  "  Lines : %d", game.lines);
		if (zenMode)
			print_line(6, "  Speed : fixed");
		else
			print_line(6, "  Level : %d", game.level + 1);
		if (game.over) {
			print_line(9,  "  GAME OVER");
			print_line(11, "  A - Play again   START - Menu");
		} else if (paused) {
			print_line(9,  "  PAUSED");
			print_line(11, "  START - Resume   SELECT - Menu");
		} else {
			print_line(9,  "");
			print_line(11, "  START - Pause");
		}
		break;

	case MODE_VERSUS:
		print_line(2,  "  VERSUS            Wins %d - %d", wins, losses);
		print_line(4,  "  You      Score %-8lu Lines %d", (unsigned long)game.score, game.lines);
		if (haveOpp)
			print_line(5, "  Rival    Score %-8lu Lines %d", (unsigned long)opp.score, opp.lines);
		else
			print_line(5, "  Rival    waiting...");
		print_line(7,  "  Incoming garbage: %d", game.pendingGarbage);
		print_line(8,  "  Garbage sent:     %d", game.garbageOut);
		switch (myStatus) {
		case ST_PLAYING: print_line(11, ""); print_line(13, ""); break;
		case ST_DEAD:    print_line(11, "  You topped out!"); print_line(13, "  A - Ready for rematch"); break;
		case ST_WON:     print_line(11, "  You win!");        print_line(13, "  A - Ready for rematch"); break;
		case ST_READY:   print_line(11, "  Ready.");          print_line(13, "  Waiting for rival..."); break;
		}
		print_line(16, paused ? "  Leave the match?  A - Yes  B - No" : "  START - Leave match");
		break;
	}
}

/* ------------------------------------------------------------------ */

int main(void)
{
	gfxInitDefault();
	gfxSetDoubleBuffering(GFX_TOP, true);
	consoleInit(GFX_BOTTOM, NULL);

	bool netOk = net_init();
	mode = MODE_MENU;
	int frame = 0;
	int scanCooldown = 0;

	while (aptMainLoop()) {
		hidScanInput();
		u32 down = hidKeysDown();
		u32 held = hidKeysHeld();
		GameInput in;
		map_input(down, held, &in);

		switch (mode) {
		case MODE_MENU:
			if (down & KEY_START)
				goto quit;
			if (down & (KEY_A | KEY_B)) {
				mode = MODE_SOLO;
				paused = false;
				zenMode = (down & KEY_B) != 0;
				start_solo();
				consoleClear();
			} else if (down & (KEY_X | KEY_Y)) {
				if (!netOk && !(netOk = net_init())) {
					snprintf(message, sizeof(message), "Local wireless unavailable.");
				} else if (down & KEY_X) {
					if (net_host_start()) {
						mode = MODE_HOST_WAIT;
						consoleClear();
					} else {
						snprintf(message, sizeof(message), "Could not create network.");
					}
				} else {
					mode = MODE_JOIN_SCAN;
					scanCooldown = 0;
					consoleClear();
				}
			}
			break;

		case MODE_HOST_WAIT:
			if (down & KEY_B)
				back_to_menu("");
			else if (net_peer_connected())
				start_versus();
			break;

		case MODE_JOIN_SCAN:
			if (down & KEY_B) {
				back_to_menu("");
			} else if (--scanCooldown <= 0) {
				scanCooldown = 60;
				int r = net_join_try();
				if (r > 0)
					start_versus();
				else if (r < 0)
					back_to_menu("Scan failed.");
			}
			break;

		case MODE_SOLO:
			if (game.over) {
				if (down & KEY_A)
					start_solo();
				else if (down & KEY_START)
					back_to_menu("");
			} else if (paused) {
				if (down & KEY_START)
					paused = false;
				else if (down & KEY_SELECT)
					back_to_menu("");
			} else if (down & KEY_START) {
				paused = true;
			} else {
				game_update(&game, &in);
			}
			break;

		case MODE_VERSUS:
			poll_network();
			if (paused) {
				/* The match keeps running; this is only a leave prompt. */
				if (down & KEY_A) {
					back_to_menu("Left the match.");
					break;
				}
				if (down & (KEY_B | KEY_START))
					paused = false;
				in.pressed = 0;
				in.held = 0;
			} else if (down & KEY_START) {
				paused = true;
			}

			if (myStatus == ST_PLAYING) {
				game_update(&game, &in);
				if (game.over) {
					myStatus = ST_DEAD;
					losses++;
				}
			} else if ((myStatus == ST_DEAD || myStatus == ST_WON) && !paused && (down & KEY_A)) {
				myStatus = ST_READY;
			}

			if (frame % 3 == 0)
				send_state();

			if (framesSinceRecv > PEER_TIMEOUT_FRAMES || (haveOpp && !net_peer_connected()))
				back_to_menu("Connection lost.");
			break;
		}

		/* Render top screen. */
		fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
		memset(fb, 0, TOP_W * TOP_H * 3);
		if (mode == MODE_SOLO || mode == MODE_VERSUS) {
			draw_game(&game);
			if (mode == MODE_VERSUS) {
				if (haveOpp)
					draw_opponent(opp.cells, opp.round == myRound && opp.status == ST_DEAD);
				else
					draw_board_bg(OX, OY, OCELL);
			}
		} else {
			/* Little decorative title made of blocks. */
			static const char *logo[5] = {
				"TTT EEE TTT RR  I SSS",
				" T  E    T  R R I S  ",
				" T  EE   T  RR  I SSS",
				" T  E    T  R R I   S",
				" T  EEE  T  R R I SSS",
			};
			for (int r = 0; r < 5; r++)
				for (int c = 0; logo[r][c]; c++)
					if (logo[r][c] != ' ')
						draw_cell(60 + c * 13, 70 + r * 13, 13, (u8)(1 + (c / 4) % 7));
		}

		draw_bottom(frame);

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
		frame++;
	}

quit:
	net_exit();
	gfxExit();
	return 0;
}
