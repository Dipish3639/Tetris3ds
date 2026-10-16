/* Host-side preview: renders sample screens to PPM files so the look can be
 * checked without a 3DS. Build: see tools/preview.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render.h"

static uint8_t top[TOP_W * TOP_H * 3], bot[BOT_W * BOT_H * 3];

static void save(const char *name, const uint8_t *buf, int w, int h)
{
	FILE *f = fopen(name, "wb");
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) {
		uint8_t px[3] = { buf[i * 3 + 2], buf[i * 3 + 1], buf[i * 3] };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}

static void shot(const char *prefix, const UiState *ui)
{
	Canvas ct, cb;
	cv_init_linear(&ct, top, TOP_W, TOP_H);
	cv_init_linear(&cb, bot, BOT_W, BOT_H);
	render_background(&ct, true);
	render_background(&cb, false);
	render_top(&ct, ui);
	render_bottom(&cb, ui);
	char name[256];
	snprintf(name, sizeof(name), "%s_top.ppm", prefix);
	save(name, top, TOP_W, TOP_H);
	snprintf(name, sizeof(name), "%s_bot.ppm", prefix);
	save(name, bot, BOT_W, BOT_H);
}

static void fill_stack(Game *g, int height, int hole)
{
	for (int r = BOARD_H - height; r < BOARD_H; r++)
		for (int c = 0; c < BOARD_W; c++)
			if (c != hole && !((r * 7 + c * 3) % 11 == 0 && r < BOARD_H - 2))
				g->cells[r][c] = (uint8_t)(1 + (r * 3 + c / 3) % 7);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char p[256];
	UiState ui;
	Game g;

	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_MENU; ui.frame = 200; ui.menuSel = 0;
	snprintf(p, sizeof(p), "%s/menu", dir); shot(p, &ui);

	game_init(&g, 42);
	fill_stack(&g, 6, 8);
	for (int r = BOARD_H - 3; r < BOARD_H; r++) g.cells[r][8] = 0;
	g.cells[BOARD_H - 1][2] = 8; g.cells[BOARD_H - 2][2] = 8;
	g.hold = 5; g.y = 6; g.x = 3;
	g.score = 18420; g.lines = 37; g.level = 3; g.pieces = 104; g.frames = 60 * 71 + 23;
	g.pendingGarbage = 0; g.b2b = 2;
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_PLAY; ui.frame = 100; ui.game = &g;
	ui.actionTimer = 70; ui.actionColor = 0xC24BD8;
	strcpy(ui.actionTop, "T-SPIN"); strcpy(ui.actionMain, "DOUBLE"); strcpy(ui.actionSub, "B2B X2");
	snprintf(p, sizeof(p), "%s/solo", dir); shot(p, &ui);

	/* Versus with incoming garbage and danger. */
	Game v;
	game_init(&v, 7);
	fill_stack(&v, 16, 3);
	v.y = 1; v.pendingGarbage = 5; v.garbageOut = 12; v.score = 9100; v.lines = 21; v.pieces = 60; v.frames = 3000;
	uint8_t opp[VISIBLE_H * BOARD_W] = { 0 };
	for (int r = 12; r < VISIBLE_H; r++)
		for (int c = 0; c < BOARD_W; c++)
			opp[r * BOARD_W + c] = (c == (r % 3) * 3) ? 0 : (r > 16 ? 8 : 1 + (r + c) % 7);
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_PLAY; ui.frame = 10; ui.game = &v; ui.versus = true;
	ui.haveOpp = true; ui.oppCells = opp; ui.oppLines = 14; ui.oppScore = 6400; ui.wins = 2; ui.losses = 1;
	ui.actionTimer = 90; strcpy(ui.actionMain, "QUAD"); strcpy(ui.actionSub, "3 COMBO");
	ui.flashTimer = 8; ui.flashRows = 1u << 21;
	snprintf(p, sizeof(p), "%s/versus", dir); shot(p, &ui);

	ui.result = RES_WIN; ui.oppDead = true; ui.actionTimer = 0; ui.flashTimer = 0;
	snprintf(p, sizeof(p), "%s/win", dir); shot(p, &ui);

	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_HOST_WAIT; ui.frame = 40;
	snprintf(p, sizeof(p), "%s/host", dir); shot(p, &ui);

	game_init(&g, 9);
	memset(&ui, 0, sizeof(ui));
	ui.screen = SCR_PLAY; ui.game = &g; ui.countdown = 70; ui.zen = true;
	snprintf(p, sizeof(p), "%s/ready", dir); shot(p, &ui);
	return 0;
}
