#ifndef GAME_H
#define GAME_H

#include <stdbool.h>
#include <stdint.h>

#define BOARD_W   10
#define BOARD_H   22   /* top 2 rows are hidden spawn rows */
#define VISIBLE_H 20
#define HIDDEN_H  (BOARD_H - VISIBLE_H)
#define NEXT_COUNT 3

#define CELL_EMPTY   0
#define CELL_GARBAGE 8 /* 1..7 are piece colours (type + 1) */

/* Input bits, independent of the 3DS HID layer. */
enum {
	IN_LEFT   = 1 << 0,
	IN_RIGHT  = 1 << 1,
	IN_DOWN   = 1 << 2,
	IN_HARD   = 1 << 3,
	IN_ROT_CW = 1 << 4,
	IN_ROT_CCW= 1 << 5,
	IN_HOLD   = 1 << 6,
};

typedef struct {
	uint32_t held;
	uint32_t pressed;
} GameInput;

typedef struct {
	uint8_t cells[BOARD_H][BOARD_W];

	int type, rot, x, y;   /* active piece */
	int hold;              /* -1 = none */
	bool holdUsed;

	uint8_t bag[7];
	int bagPos;
	int queue[NEXT_COUNT];

	int gravityTimer;
	int lockTimer;
	int lockResets;
	int dasTimer;
	int dasDir;

	uint32_t score;
	int lines;
	int level;
	bool over;
	bool zen;              /* gravity never speeds up */

	int pendingGarbage;    /* lines queued to be pushed up from below */
	int garbageOut;        /* lines sent to the opponent (cumulative this round) */
	int lastClear;         /* lines cleared by the most recent lock (for UI) */

	uint32_t rng;
} Game;

void game_init(Game *g, uint32_t seed);
void game_update(Game *g, const GameInput *in);
void game_add_garbage(Game *g, int lines);

/* Shape helpers for rendering. */
bool piece_cell(int type, int rot, int r, int c);
int  game_ghost_y(const Game *g);

#endif
