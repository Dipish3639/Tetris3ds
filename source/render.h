#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "game.h"
#include "gfx.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

enum { SCR_MENU, SCR_HOST_WAIT, SCR_JOIN_SCAN, SCR_PLAY };
enum { RES_NONE, RES_GAMEOVER, RES_WIN, RES_LOSE, RES_READY };
enum { MENU_MARATHON, MENU_ZEN, MENU_HOST, MENU_JOIN, MENU_COUNT };

typedef struct {
	int screen;
	int frame;

	/* Menu */
	int menuSel;
	const char *message;

	/* Play */
	const Game *game;
	bool zen, versus, paused;
	int countdown;          /* frames left of READY / GO, 0 when playing */
	int result;

	int flashTimer;         /* line clear flash */
	uint32_t flashRows;
	int actionTimer;        /* "T-SPIN DOUBLE" style popups */
	char actionTop[24], actionMain[24], actionSub[24];
	uint32_t actionColor;
	bool actionAllClear;

	/* Versus */
	bool haveOpp;
	const uint8_t *oppCells; /* VISIBLE_H x BOARD_W */
	bool oppDead;
	uint32_t oppScore;
	int oppLines;
	int wins, losses;
	bool oppReady;
	bool lastWin;            /* outcome of the round, kept while READY */
} UiState;

void render_background(Canvas *cv, bool top);
void render_top(Canvas *cv, const UiState *ui);
void render_bottom(Canvas *cv, const UiState *ui);

/* Touch hit testing for the bottom screen menu; returns MENU_* or -1. */
int menu_hit(int x, int y);

#endif
