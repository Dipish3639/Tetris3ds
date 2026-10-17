#include "render.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Palette (TETR.IO-like). */

static const uint32_t MINO[9] = {
	0x000000,
	0x3DD9B6, /* I */
	0x5662E8, /* J */
	0xF08A3C, /* L */
	0xF2CB3A, /* O */
	0x8FD13F, /* S */
	0xC24BD8, /* T */
	0xEB4B57, /* Z */
	0x6E6E6E, /* garbage */
};

#define COL_TEXT      0xFFFFFF
#define COL_LABEL     0xB8B8C8
#define COL_DIM       0x8A8AA0
#define COL_PANEL     0x07070C
#define COL_BORDER    0xD8D8E0
#define COL_DANGER    0xFF3040
#define COL_GOLD      0xFFD54A

static const uint32_t MENU_COLORS[MENU_COUNT] = { 0x8C5BE8, 0x2FB894, 0xE8505B, 0xE8973D };
static const char *MENU_TITLES[MENU_COUNT] = { "MARATHON", "ZEN", "HOST VERSUS", "JOIN VERSUS" };
static const char *MENU_SUBS[MENU_COUNT] = {
	"SPEED RISES EVERY 10 LINES",
	"RELAX - THE SPEED NEVER CHANGES",
	"CREATE A LOCAL WIRELESS ROOM",
	"FIND A NEARBY 3DS TO BATTLE",
};

/* ------------------------------------------------------------------ */
/* Primitives. */

static void draw_mino(Canvas *cv, int x, int y, int s, uint32_t c)
{
	cv_fill(cv, x, y, s, s, c);
	int band = s >= 9 ? 2 : 1;
	cv_fill(cv, x, y, s, band, rgb_mix(c, 0xFFFFFF, 90));
	cv_fill(cv, x, y + band, 1, s - band, rgb_mix(c, 0xFFFFFF, 40));
	cv_fill(cv, x, y + s - 1, s, 1, rgb_mix(c, 0x000000, 80));
	cv_fill(cv, x + s - 1, y + band, 1, s - band - 1, rgb_mix(c, 0x000000, 50));
}

static void piece_bounds(int type, int *c0, int *r0, int *w, int *h)
{
	int minr = 4, maxr = -1, minc = 4, maxc = -1;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (piece_cell(type, 0, r, c)) {
				if (r < minr) minr = r;
				if (r > maxr) maxr = r;
				if (c < minc) minc = c;
				if (c > maxc) maxc = c;
			}
	*c0 = minc; *r0 = minr; *w = maxc - minc + 1; *h = maxr - minr + 1;
}

/* Draw a piece centred on (cx, cy). */
static void draw_piece_centered(Canvas *cv, int cx, int cy, int s, int type, uint32_t override)
{
	if (type < 0)
		return;
	int c0, r0, w, h;
	piece_bounds(type, &c0, &r0, &w, &h);
	int ox = cx - w * s / 2, oy = cy - h * s / 2;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++)
			if (piece_cell(type, 0, r, c))
				draw_mino(cv, ox + (c - c0) * s, oy + (r - r0) * s, s,
				          override ? override : MINO[type + 1]);
}

static void label(Canvas *cv, int x, int y, const char *s, int align)
{
	cv_text(cv, &FONT_SMALL, x, y, s, COL_LABEL, 255, 1, align);
}

static void panel(Canvas *cv, int x, int y, int w, int h, int alpha)
{
	cv_blend(cv, x, y, w, h, COL_PANEL, alpha);
}

static int pulse(int frame, int period)
{
	int t = frame % period;
	int half = period / 2;
	return (t < half ? t : period - t) * 255 / half;
}

static void format_time(char *buf, size_t n, int frames, bool fraction)
{
	int cs = frames * 100 / 60;
	if (fraction)
		snprintf(buf, n, "%d:%02d.%02d", cs / 6000, (cs / 100) % 60, cs % 100);
	else
		snprintf(buf, n, "%d:%02d", cs / 6000, (cs / 100) % 60);
}

/* ------------------------------------------------------------------ */
/* Backgrounds (cached by the caller). */

void render_background(Canvas *cv, bool top)
{
	cv_vgradient(cv, 0, 0, cv->w, cv->h, top ? 0x1B1636 : 0x15122A, top ? 0x08070F : 0x07060C);
	/* Soft diagonal light bands. */
	for (int i = 0; i < 3; i++) {
		int x0 = 40 + i * 140;
		for (int y = 0; y < cv->h; y++)
			cv_blend(cv, x0 - y / 2, y, 50 + i * 10, 1, 0x6A5ACD, 10);
	}
	/* Faint dot grid. */
	for (int y = 6; y < cv->h; y += 12)
		for (int x = 6; x < cv->w; x += 12)
			cv_blend(cv, x, y, 1, 1, 0xFFFFFF, 18);
}

static void falling_minos(Canvas *cv, int frame, int alpha)
{
	for (int i = 0; i < 16; i++) {
		int s = 10 + (i % 3) * 4;
		int speed = 1 + i % 3;
		int x = (i * 97 + 13) % (cv->w + 20) - 10;
		int y = ((frame * speed) / 3 + i * 53) % (cv->h + 60) - 40;
		int type = i % 7;
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++)
				if (piece_cell(type, i % 4, r, c))
					cv_blend(cv, x + c * s, y + r * s, s - 1, s - 1, MINO[type + 1], alpha);
	}
}

/* ------------------------------------------------------------------ */
/* Boards. */

#define CELL 11
#define BX   145
#define BY   15
#define BW   (BOARD_W * CELL)
#define BH   (VISIBLE_H * CELL)

static bool in_danger(const Game *g)
{
	for (int r = HIDDEN_H; r < HIDDEN_H + 5; r++)
		for (int c = 0; c < BOARD_W; c++)
			if (g->cells[r][c])
				return true;
	return false;
}

static void board_frame(Canvas *cv, int x, int y, int cell, uint32_t border, int borderAlpha, int grid)
{
	int w = BOARD_W * cell, h = VISIBLE_H * cell;
	panel(cv, x, y, w, h, 225);
	if (grid)
		for (int c = 1; c < BOARD_W; c++)
			cv_blend(cv, x + c * cell, y, 1, h, 0xFFFFFF, grid);
	if (grid)
		for (int r = 1; r < VISIBLE_H; r++)
			cv_blend(cv, x, y + r * cell, w, 1, 0xFFFFFF, grid);
	/* Open-topped border: left, right and bottom. */
	cv_blend(cv, x - 2, y, 2, h + 2, border, borderAlpha);
	cv_blend(cv, x + w, y, 2, h + 2, border, borderAlpha);
	cv_blend(cv, x - 2, y + h, w + 4, 2, border, borderAlpha);
}

static void board_cells(Canvas *cv, int x, int y, int cell, const uint8_t *cells, bool dead)
{
	for (int r = 0; r < VISIBLE_H; r++)
		for (int c = 0; c < BOARD_W; c++) {
			uint8_t v = cells[r * BOARD_W + c];
			if (v > CELL_GARBAGE)
				v = CELL_GARBAGE;
			if (!v)
				continue;
			uint32_t col = dead ? rgb_mix(MINO[v], 0x404048, 200) : MINO[v];
			if (cell >= 8)
				draw_mino(cv, x + c * cell, y + r * cell, cell, col);
			else
				cv_fill(cv, x + c * cell, y + r * cell, cell, cell, col);
		}
}

static void draw_play_top(Canvas *cv, const UiState *ui)
{
	const Game *g = ui->game;
	bool won = ui->result == RES_WIN || (ui->result == RES_READY && ui->lastWin);
	bool dead = ui->result != RES_NONE && !won;
	bool danger = !dead && in_danger(g);

	uint32_t border = COL_BORDER;
	int borderAlpha = 200;
	if (danger) {
		border = rgb_mix(COL_BORDER, COL_DANGER, 120 + pulse(ui->frame, 40) / 2);
		borderAlpha = 255;
	}
	board_frame(cv, BX, BY, CELL, border, borderAlpha, 14);
	if (danger)
		cv_blend(cv, BX, BY, BW, BH, COL_DANGER, 10 + pulse(ui->frame, 40) / 16);

	board_cells(cv, BX, BY, CELL, &g->cells[HIDDEN_H][0], dead);

	/* Ghost and active piece. */
	if (!g->over && ui->countdown <= 30 && ui->result == RES_NONE) {
		uint32_t col = MINO[g->type + 1];
		int gy = game_ghost_y(g);
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				if (!piece_cell(g->type, g->rot, r, c))
					continue;
				int sx = BX + (g->x + c) * CELL;
				int gr = gy + r - HIDDEN_H;
				if (gr >= 0) {
					cv_blend(cv, sx, BY + gr * CELL, CELL, CELL, col, 60);
					cv_frame(cv, sx, BY + gr * CELL, CELL, CELL, col, 110);
				}
			}
		for (int r = 0; r < 4; r++)
			for (int c = 0; c < 4; c++) {
				int pr = g->y + r - HIDDEN_H;
				if (piece_cell(g->type, g->rot, r, c) && pr >= -1) {
					int py = BY + pr * CELL;
					/* Pieces poking above the board are drawn too, like TETR.IO. */
					draw_mino(cv, BX + (g->x + c) * CELL, py, CELL,
					          g->lockTimer > 0 ? rgb_mix(col, 0xFFFFFF, g->lockTimer * 3) : col);
				}
			}
	}

	/* Line clear flash. */
	if (ui->flashTimer > 0)
		for (int r = HIDDEN_H; r < BOARD_H; r++)
			if (ui->flashRows & (1u << r)) {
				int a = ui->flashTimer * 255 / 14;
				int grow = (14 - ui->flashTimer) / 2;
				cv_blend(cv, BX - grow, BY + (r - HIDDEN_H) * CELL - grow / 2,
				         BW + grow * 2, CELL + grow, 0xFFFFFF, a);
			}

	/* Garbage meter. */
	cv_blend(cv, BX - 8, BY, 4, BH, 0x000000, 160);
	int pend = g->pendingGarbage > VISIBLE_H ? VISIBLE_H : g->pendingGarbage;
	if (pend > 0) {
		uint32_t gc = pend >= 4 ? rgb_mix(COL_DANGER, 0xFFFFFF, pulse(ui->frame, 20) / 3) : COL_DANGER;
		cv_fill(cv, BX - 8, BY + BH - pend * CELL, 4, pend * CELL, gc);
	}

	/* HOLD panel. */
	int hx = BX - 66, hw = 56;
	panel(cv, hx, BY, hw, 46, 210);
	cv_blend(cv, hx, BY, hw, 1, COL_BORDER, 60);
	label(cv, hx + 5, BY + 4, "HOLD", ALIGN_LEFT);
	draw_piece_centered(cv, hx + hw / 2, BY + 29, 9, g->hold,
	                    g->holdUsed && g->hold >= 0 ? 0x4A4A55 : 0);

	/* NEXT panel. */
	int nx = BX + BW + 10, nw = 56;
	panel(cv, nx, BY, nw, 142, 210);
	cv_blend(cv, nx, BY, nw, 1, COL_BORDER, 60);
	label(cv, nx + nw - 5, BY + 4, "NEXT", ALIGN_RIGHT);
	for (int i = 0; i < NEXT_COUNT; i++)
		draw_piece_centered(cv, nx + nw / 2, BY + 30 + i * 24, i == 0 ? 9 : 8, g->queue[i], 0);

	/* Stats, left column, right-aligned to the board like TETR.IO. */
	char buf[32];
	int sx = BX - 12;
	int sy = BH + BY - 82;
	float secs = g->frames / 60.0f;
	label(cv, sx, sy, "PIECES", ALIGN_RIGHT);
	snprintf(buf, sizeof(buf), "%d, %.2f/S", g->pieces, secs > 0.5f ? g->pieces / secs : 0.0f);
	cv_text(cv, &FONT_MED, sx, sy + 11, buf, COL_TEXT, 255, 0, ALIGN_RIGHT);
	label(cv, sx, sy + 30, "LINES", ALIGN_RIGHT);
	snprintf(buf, sizeof(buf), "%d", g->lines);
	cv_text(cv, &FONT_MED, sx, sy + 41, buf, COL_TEXT, 255, 0, ALIGN_RIGHT);
	label(cv, sx, sy + 60, "TIME", ALIGN_RIGHT);
	format_time(buf, sizeof(buf), g->frames, true);
	cv_text(cv, &FONT_MED, sx, sy + 71, buf, COL_TEXT, 255, 0, ALIGN_RIGHT);

	/* Right column under NEXT. */
	int rx = nx + 2, ry = BY + 154;
	label(cv, rx, ry, "SCORE", ALIGN_LEFT);
	snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->score);
	cv_text(cv, &FONT_MED, rx, ry + 11, buf, COL_TEXT, 255, 0, ALIGN_LEFT);
	if (ui->versus) {
		label(cv, rx, ry + 30, "ATTACK", ALIGN_LEFT);
		snprintf(buf, sizeof(buf), "%d", g->garbageOut);
	} else if (ui->zen) {
		label(cv, rx, ry + 30, "MODE", ALIGN_LEFT);
		snprintf(buf, sizeof(buf), "ZEN");
	} else {
		label(cv, rx, ry + 30, "LEVEL", ALIGN_LEFT);
		snprintf(buf, sizeof(buf), "%d", g->level + 1);
	}
	cv_text(cv, &FONT_MED, rx, ry + 41, buf, COL_TEXT, 255, 0, ALIGN_LEFT);

	/* Action text popup, left of the board. */
	if (ui->actionTimer > 0) {
		int a = ui->actionTimer > 20 ? 255 : ui->actionTimer * 255 / 20;
		int slide = ui->actionTimer > 80 ? (ui->actionTimer - 80) : 0;
		int ax = BX - 14 + slide, ay = BY + 60;
		if (ui->actionTop[0])
			cv_text(cv, &FONT_SMALL, ax, ay, ui->actionTop, ui->actionColor, a, 1, ALIGN_RIGHT);
		if (ui->actionMain[0])
			cv_text(cv, &FONT_BIG, ax, ay + 12, ui->actionMain, COL_TEXT, a, 0, ALIGN_RIGHT);
		if (ui->actionSub[0])
			cv_text(cv, &FONT_MED, ax, ay + 34, ui->actionSub, COL_GOLD, a, 0, ALIGN_RIGHT);
	}
	if (ui->actionAllClear && ui->actionTimer > 0) {
		int a = ui->actionTimer > 20 ? 255 : ui->actionTimer * 255 / 20;
		cv_text(cv, &FONT_BIG, BX + BW / 2, BY + 70, "ALL", COL_GOLD, a, 2, ALIGN_CENTER);
		cv_text(cv, &FONT_BIG, BX + BW / 2, BY + 92, "CLEAR", COL_GOLD, a, 2, ALIGN_CENTER);
	}

	/* Overlays. */
	int cx = BX + BW / 2, cy = BY + BH / 2;
	if (ui->countdown > 0) {
		bool go = ui->countdown <= 30;
		const char *t = go ? "GO!" : "READY";
		int a = go ? ui->countdown * 255 / 30 : 255;
		cv_text(cv, go ? &FONT_HUGE : &FONT_BIG, cx, cy - (go ? 12 : 8), t,
		        go ? COL_GOLD : COL_TEXT, a, 2, ALIGN_CENTER);
	} else if (ui->result != RES_NONE) {
		cv_blend(cv, BX, BY, BW, BH, 0x000000, 120);
		const char *l1 = "GAME", *l2 = "OVER";
		uint32_t col = COL_TEXT;
		if (won) {
			l1 = "YOU"; l2 = "WIN"; col = COL_GOLD;
		} else if (ui->versus) {
			l1 = "TOP"; l2 = "OUT"; col = COL_DANGER;
		}
		cv_text(cv, &FONT_BIG, cx, cy - 20, l1, col, 255, 2, ALIGN_CENTER);
		cv_text(cv, &FONT_BIG, cx, cy + 4, l2, col, 255, 2, ALIGN_CENTER);
	} else if (ui->paused && !ui->versus) {
		cv_blend(cv, BX, BY, BW, BH, 0x000000, 170);
		cv_text(cv, &FONT_BIG, cx, cy - 8, "PAUSED", COL_TEXT, 255, 2, ALIGN_CENTER);
	}
}

/* ------------------------------------------------------------------ */
/* Title. */

static void draw_title(Canvas *cv, int cx, int y, int frame)
{
	static const char *word = "TETRIS";
	static const int colorFor[6] = { 7, 3, 4, 5, 1, 6 };
	int w = text_width(&FONT_HUGE, word, 3);
	int x = cx - w / 2;
	for (int i = 0; word[i]; i++) {
		char ch[2] = { word[i], 0 };
		int bob = (pulse(frame + i * 8, 120) - 128) / 64;
		cv_text(cv, &FONT_HUGE, x + 2, y + 3 + bob, ch, 0x000000, 140, 0, ALIGN_LEFT);
		cv_text(cv, &FONT_HUGE, x, y + bob, ch, MINO[colorFor[i]], 255, 0, ALIGN_LEFT);
		x += text_width(&FONT_HUGE, ch, 0) + 3;
	}
	cv_text(cv, &FONT_MED, cx, y + 36, "3 D S   E D I T I O N", COL_LABEL, 255, 0, ALIGN_CENTER);
}

static void spinner(Canvas *cv, int cx, int cy, int frame)
{
	static const int pos[4][2] = { {0,0}, {1,0}, {1,1}, {0,1} };
	int step = (frame / 8) % 4;
	for (int i = 0; i < 4; i++) {
		int a = i == step ? 255 : 70;
		cv_blend(cv, cx - 12 + pos[i][0] * 13, cy - 12 + pos[i][1] * 13, 11, 11, MINO[1 + (i + 4) % 7], a);
	}
}

void render_top(Canvas *cv, const UiState *ui)
{
	switch (ui->screen) {
	case SCR_MENU:
		falling_minos(cv, ui->frame, 28);
		draw_title(cv, TOP_W / 2, 80, ui->frame);
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 196, "USE THE TOUCH SCREEN OR D-PAD + A", COL_DIM, 255, 1, ALIGN_CENTER);
		if (ui->audioOk) {
			char buf[48];
			snprintf(buf, sizeof(buf), "X: MUSIC %s     Y: SOUND %s",
			         ui->musicOn ? "ON" : "OFF", ui->sfxOn ? "ON" : "OFF");
			cv_text(cv, &FONT_SMALL, TOP_W / 2, 212, buf, COL_LABEL, 255, 1, ALIGN_CENTER);
		} else {
			cv_text(cv, &FONT_SMALL, TOP_W / 2, 212, "AUDIO OFF: /3DS/DSPFIRM.CDC NOT FOUND (RUN DSP1)",
			        COL_DANGER, 255, 1, ALIGN_CENTER);
		}
		break;
	case SCR_LOADING:
		draw_title(cv, TOP_W / 2, 80, 0);
		cv_text(cv, &FONT_SMALL, TOP_W / 2, 180, "TUNING UP...", COL_LABEL, 255, 2, ALIGN_CENTER);
		break;
	case SCR_HOST_WAIT:
	case SCR_JOIN_SCAN:
		falling_minos(cv, ui->frame, 20);
		draw_title(cv, TOP_W / 2, 50, ui->frame);
		spinner(cv, TOP_W / 2, 150, ui->frame);
		cv_text(cv, &FONT_MED, TOP_W / 2, 180,
		        ui->screen == SCR_HOST_WAIT ? "WAITING FOR A CHALLENGER" : "SEARCHING FOR A ROOM",
		        COL_TEXT, 255, 1, ALIGN_CENTER);
		break;
	case SCR_PLAY:
		draw_play_top(cv, ui);
		break;
	}
}

/* ------------------------------------------------------------------ */
/* Bottom screen. */

#define CARD_X 14
#define CARD_W (BOT_W - 28)
#define CARD_H 40
#define CARD_Y0 33
#define CARD_GAP 5

int menu_hit(int x, int y)
{
	for (int i = 0; i < MENU_COUNT; i++) {
		int cy = CARD_Y0 + i * (CARD_H + CARD_GAP);
		if (x >= CARD_X && x < CARD_X + CARD_W && y >= cy && y < cy + CARD_H)
			return i;
	}
	return -1;
}

static void header(Canvas *cv, const char *title, uint32_t accent)
{
	cv_blend(cv, 0, 0, BOT_W, 24, 0x000000, 150);
	cv_fill(cv, 0, 24, BOT_W, 2, accent);
	cv_text(cv, &FONT_MED, 12, 7, title, COL_TEXT, 255, 1, ALIGN_LEFT);
}

static void footer(Canvas *cv, const char *left, const char *right)
{
	cv_blend(cv, 0, BOT_H - 18, BOT_W, 18, 0x000000, 150);
	if (left)
		cv_text(cv, &FONT_SMALL, 10, BOT_H - 12, left, COL_DIM, 255, 1, ALIGN_LEFT);
	if (right)
		cv_text(cv, &FONT_SMALL, BOT_W - 10, BOT_H - 12, right, COL_DIM, 255, 1, ALIGN_RIGHT);
}

static void card(Canvas *cv, int x, int y, int w, int h, uint32_t accent, bool selected)
{
	cv_blend(cv, x, y, w, h, rgb_mix(0x101018, accent, selected ? 70 : 25), selected ? 240 : 210);
	cv_fill(cv, x, y, 5, h, accent);
	if (selected)
		cv_frame(cv, x, y, w, h, rgb_mix(accent, 0xFFFFFF, 120), 255);
}

static void stat_box(Canvas *cv, int x, int y, int w, const char *name, const char *value)
{
	cv_blend(cv, x, y, w, 34, 0x000000, 130);
	cv_text(cv, &FONT_SMALL, x + 7, y + 6, name, COL_LABEL, 255, 1, ALIGN_LEFT);
	cv_text(cv, &FONT_MED, x + 7, y + 18, value, COL_TEXT, 255, 0, ALIGN_LEFT);
}

static void draw_menu_bottom(Canvas *cv, const UiState *ui)
{
	header(cv, "MAIN MENU", MENU_COLORS[ui->menuSel]);
	for (int i = 0; i < MENU_COUNT; i++) {
		int y = CARD_Y0 + i * (CARD_H + CARD_GAP);
		bool sel = i == ui->menuSel;
		card(cv, CARD_X, y, CARD_W, CARD_H, MENU_COLORS[i], sel);
		cv_text(cv, &FONT_BIG, CARD_X + 16, y + 7, MENU_TITLES[i], COL_TEXT, 255, 1, ALIGN_LEFT);
		cv_text(cv, &FONT_SMALL, CARD_X + 16, y + 27, MENU_SUBS[i],
		        sel ? 0xE0E0F0 : COL_DIM, 255, 1, ALIGN_LEFT);
		/* Small piece icon on the right. */
		draw_piece_centered(cv, CARD_X + CARD_W - 28, y + CARD_H / 2, 7, (i * 2 + 5) % 7, 0);
	}
	if (ui->message && ui->message[0])
		footer(cv, ui->message, "START: EXIT");
	else
		footer(cv, "D-PAD + A OR TAP TO SELECT", "START: EXIT");
}

static void draw_wait_bottom(Canvas *cv, const UiState *ui)
{
	bool host = ui->screen == SCR_HOST_WAIT;
	uint32_t accent = MENU_COLORS[host ? MENU_HOST : MENU_JOIN];
	header(cv, host ? "HOST VERSUS" : "JOIN VERSUS", accent);
	card(cv, 14, 40, BOT_W - 28, 110, accent, false);
	spinner(cv, BOT_W / 2, 74, ui->frame);
	cv_text(cv, &FONT_BIG, BOT_W / 2, 100, host ? "ROOM OPEN" : "SCANNING", COL_TEXT, 255, 1, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 128,
	        host ? "ON THE OTHER 3DS CHOOSE JOIN VERSUS" : "ON THE OTHER 3DS CHOOSE HOST VERSUS",
	        COL_LABEL, 255, 1, ALIGN_CENTER);
	cv_text(cv, &FONT_SMALL, BOT_W / 2, 170, "CLEAR 2+ LINES OR T-SPIN TO SEND GARBAGE", COL_DIM, 255, 1, ALIGN_CENTER);
	footer(cv, "B: CANCEL", NULL);
}

static void draw_solo_bottom(Canvas *cv, const UiState *ui)
{
	const Game *g = ui->game;
	uint32_t accent = MENU_COLORS[ui->zen ? MENU_ZEN : MENU_MARATHON];
	header(cv, ui->zen ? "ZEN" : "MARATHON", accent);

	char buf[32];
	label(cv, BOT_W / 2, 38, "SCORE", ALIGN_CENTER);
	snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->score);
	cv_text(cv, &FONT_HUGE, BOT_W / 2, 52, buf, COL_TEXT, 255, 1, ALIGN_CENTER);

	int w = (BOT_W - 28 - 12) / 3;
	float secs = g->frames / 60.0f;
	snprintf(buf, sizeof(buf), "%d", g->lines);
	stat_box(cv, 14, 98, w, "LINES", buf);
	if (ui->zen)
		snprintf(buf, sizeof(buf), "FIXED");
	else
		snprintf(buf, sizeof(buf), "%d", g->level + 1);
	stat_box(cv, 14 + w + 6, 98, w, ui->zen ? "SPEED" : "LEVEL", buf);
	format_time(buf, sizeof(buf), g->frames, false);
	stat_box(cv, 14 + (w + 6) * 2, 98, w, "TIME", buf);
	snprintf(buf, sizeof(buf), "%d", g->pieces);
	stat_box(cv, 14, 138, w, "PIECES", buf);
	snprintf(buf, sizeof(buf), "%.2f", secs > 0.5f ? g->pieces / secs : 0.0f);
	stat_box(cv, 14 + w + 6, 138, w, "PPS", buf);
	snprintf(buf, sizeof(buf), "%d", g->b2b > 0 ? g->b2b : 0);
	stat_box(cv, 14 + (w + 6) * 2, 138, w, "B2B", buf);

	if (ui->result == RES_GAMEOVER) {
		card(cv, 14, 180, BOT_W - 28, 34, COL_DANGER, true);
		cv_text(cv, &FONT_MED, BOT_W / 2, 192, "GAME OVER - A: RETRY", COL_TEXT, 255, 1, ALIGN_CENTER);
		footer(cv, "START: MENU", NULL);
	} else if (ui->paused) {
		card(cv, 14, 180, BOT_W - 28, 34, accent, true);
		cv_text(cv, &FONT_MED, BOT_W / 2, 192, "PAUSED", COL_TEXT, 255, 2, ALIGN_CENTER);
		footer(cv, "START: RESUME", "SELECT: QUIT");
	} else {
		cv_text(cv, &FONT_SMALL, BOT_W / 2, 186, "UP HARD DROP   A/B ROTATE   L/R HOLD", COL_DIM, 255, 1, ALIGN_CENTER);
		cv_text(cv, &FONT_SMALL, BOT_W / 2, 200, "DOWN SOFT DROP   START PAUSE", COL_DIM, 255, 1, ALIGN_CENTER);
		footer(cv, NULL, NULL);
	}
}

static void draw_versus_bottom(Canvas *cv, const UiState *ui)
{
	const Game *g = ui->game;
	uint32_t accent = MENU_COLORS[MENU_HOST];
	header(cv, "VERSUS", accent);

	/* Rival board. */
	int ox = 22, oy = 40, oc = 9;
	cv_text(cv, &FONT_SMALL, ox, 32, "RIVAL", COL_LABEL, 255, 1, ALIGN_LEFT);
	board_frame(cv, ox, oy, oc, ui->oppDead ? COL_DANGER : COL_BORDER, 180, 10);
	if (ui->haveOpp)
		board_cells(cv, ox, oy, oc, ui->oppCells, ui->oppDead);
	else
		spinner(cv, ox + BOARD_W * oc / 2, oy + VISIBLE_H * oc / 2, ui->frame);
	if (ui->oppDead) {
		cv_blend(cv, ox, oy, BOARD_W * oc, VISIBLE_H * oc, 0x000000, 110);
		cv_text(cv, &FONT_BIG, ox + BOARD_W * oc / 2, oy + 80, "KO", COL_DANGER, 255, 2, ALIGN_CENTER);
	}

	/* Scoreboard. */
	int rx = 132, rw = BOT_W - rx - 14;
	char buf[32];
	cv_blend(cv, rx, 36, rw, 58, 0x000000, 130);
	label(cv, rx + rw / 2, 42, "YOU          RIVAL", ALIGN_CENTER);
	snprintf(buf, sizeof(buf), "%d - %d", ui->wins, ui->losses);
	cv_text(cv, &FONT_HUGE, rx + rw / 2, 58, buf, COL_TEXT, 255, 2, ALIGN_CENTER);

	int w = (rw - 6) / 2;
	snprintf(buf, sizeof(buf), "%d", g->garbageOut);
	stat_box(cv, rx, 100, w, "SENT", buf);
	snprintf(buf, sizeof(buf), "%d", g->pendingGarbage);
	stat_box(cv, rx + w + 6, 100, w, "INCOMING", buf);
	snprintf(buf, sizeof(buf), "%d", ui->haveOpp ? ui->oppLines : 0);
	stat_box(cv, rx, 140, w, "RIVAL LINES", buf);
	snprintf(buf, sizeof(buf), "%lu", (unsigned long)(ui->haveOpp ? ui->oppScore : 0));
	stat_box(cv, rx + w + 6, 140, w, "RIVAL SCORE", buf);

	const char *msg = NULL;
	uint32_t mc = accent;
	switch (ui->result) {
	case RES_WIN:  msg = "YOU WIN!  A: REMATCH"; mc = COL_GOLD; break;
	case RES_LOSE: msg = "TOPPED OUT  A: REMATCH"; mc = COL_DANGER; break;
	case RES_READY: msg = ui->oppReady ? "STARTING..." : "WAITING FOR RIVAL..."; break;
	}
	if (ui->paused) {
		msg = "LEAVE MATCH?  A: YES  B: NO";
		mc = COL_DANGER;
	}
	if (msg) {
		card(cv, rx, 180, rw, 30, mc, true);
		cv_text(cv, &FONT_SMALL, rx + rw / 2, 192, msg, COL_TEXT, 255, 1, ALIGN_CENTER);
	}
	footer(cv, "START: LEAVE", NULL);
}

void render_bottom(Canvas *cv, const UiState *ui)
{
	switch (ui->screen) {
	case SCR_LOADING:   break;
	case SCR_MENU:      draw_menu_bottom(cv, ui); break;
	case SCR_HOST_WAIT:
	case SCR_JOIN_SCAN: draw_wait_bottom(cv, ui); break;
	case SCR_PLAY:
		if (ui->versus)
			draw_versus_bottom(cv, ui);
		else
			draw_solo_bottom(cv, ui);
		break;
	}
}
