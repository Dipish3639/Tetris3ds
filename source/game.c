#include "game.h"
#include <string.h>

/* 4x4 bitmasks, bit 15 = row 0 col 0. Order: I J L O S T Z. */
static const uint16_t SHAPES[7][4] = {
	{ 0x0F00, 0x2222, 0x00F0, 0x4444 }, /* I */
	{ 0x8E00, 0x6440, 0x0E20, 0x44C0 }, /* J */
	{ 0x2E00, 0x4460, 0x0E80, 0xC440 }, /* L */
	{ 0x6600, 0x6600, 0x6600, 0x6600 }, /* O */
	{ 0x6C00, 0x4620, 0x06C0, 0x8C40 }, /* S */
	{ 0x4E00, 0x4640, 0x0E40, 0x4C40 }, /* T */
	{ 0xC600, 0x2640, 0x0C60, 0x4C80 }, /* Z */
};

/* Simple wall kicks tried in order on rotation. */
static const int KICKS[][2] = { {0,0}, {-1,0}, {1,0}, {0,-1}, {-2,0}, {2,0}, {0,1} };

/* Attack tables, roughly following TETR.IO. */
static const int ATTACK[5]      = { 0, 0, 1, 2, 4 };
static const int ATTACK_SPIN[4] = { 0, 2, 4, 6 };
static const int ATTACK_MINI[4] = { 0, 0, 1, 2 };
static const int COMBO_BONUS[]  = { 0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5 };
#define ALL_CLEAR_BONUS 10

#define DAS_DELAY   10
#define DAS_REPEAT  2
#define LOCK_DELAY  30
#define MAX_RESETS  15

static uint32_t rnd(Game *g)
{
	/* xorshift32 */
	uint32_t x = g->rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g->rng = x;
	return x;
}

bool piece_cell(int type, int rot, int r, int c)
{
	return (SHAPES[type][rot & 3] & (0x8000 >> (r * 4 + c))) != 0;
}

static bool fits(const Game *g, int type, int rot, int x, int y)
{
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			if (!piece_cell(type, rot, r, c))
				continue;
			int bx = x + c, by = y + r;
			if (bx < 0 || bx >= BOARD_W || by >= BOARD_H)
				return false;
			if (by >= 0 && g->cells[by][bx] != CELL_EMPTY)
				return false;
		}
	return true;
}

static void refill_bag(Game *g)
{
	for (int i = 0; i < 7; i++)
		g->bag[i] = (uint8_t)i;
	for (int i = 6; i > 0; i--) {
		int j = (int)(rnd(g) % (uint32_t)(i + 1));
		uint8_t t = g->bag[i];
		g->bag[i] = g->bag[j];
		g->bag[j] = t;
	}
	g->bagPos = 0;
}

static int bag_next(Game *g)
{
	if (g->bagPos >= 7)
		refill_bag(g);
	return g->bag[g->bagPos++];
}

static void spawn(Game *g, int type)
{
	g->type = type;
	g->rot = 0;
	g->x = 3;
	g->y = 0;
	g->gravityTimer = 0;
	g->lockTimer = 0;
	g->lockResets = 0;
	g->lastRotated = false;
	if (!fits(g, g->type, g->rot, g->x, g->y))
		g->over = true;
}

static void spawn_next(Game *g)
{
	int t = g->queue[0];
	for (int i = 0; i < NEXT_COUNT - 1; i++)
		g->queue[i] = g->queue[i + 1];
	g->queue[NEXT_COUNT - 1] = bag_next(g);
	g->holdUsed = false;
	spawn(g, t);
}

void game_init(Game *g, uint32_t seed)
{
	memset(g, 0, sizeof(*g));
	g->rng = seed ? seed : 0x12345678u;
	g->hold = -1;
	g->combo = -1;
	g->b2b = -1;
	g->bagPos = 7;
	for (int i = 0; i < NEXT_COUNT; i++)
		g->queue[i] = bag_next(g);
	spawn_next(g);
}

void game_add_garbage(Game *g, int lines)
{
	if (lines > 0)
		g->pendingGarbage += lines;
}

static void push_garbage(Game *g)
{
	int n = g->pendingGarbage;
	if (n <= 0)
		return;
	if (n > BOARD_H)
		n = BOARD_H;
	g->pendingGarbage = 0;
	g->garbageEvent++;

	/* Anything shoved off the top means we topped out. */
	for (int r = 0; r < n; r++)
		for (int c = 0; c < BOARD_W; c++)
			if (g->cells[r][c] != CELL_EMPTY)
				g->over = true;

	memmove(&g->cells[0][0], &g->cells[n][0], (size_t)(BOARD_H - n) * BOARD_W);

	int hole = (int)(rnd(g) % BOARD_W);
	for (int r = BOARD_H - n; r < BOARD_H; r++) {
		/* Occasionally shift the hole so big attacks aren't a single well. */
		if ((rnd(g) & 3) == 0)
			hole = (int)(rnd(g) % BOARD_W);
		for (int c = 0; c < BOARD_W; c++)
			g->cells[r][c] = (c == hole) ? CELL_EMPTY : CELL_GARBAGE;
	}
}

static bool occupied(const Game *g, int x, int y)
{
	if (x < 0 || x >= BOARD_W || y >= BOARD_H)
		return true;
	return y >= 0 && g->cells[y][x] != CELL_EMPTY;
}

/* Three-corner T-spin rule. Every T rotation here has its centre at (1,1). */
static int detect_spin(const Game *g)
{
	if (g->type != 5 || !g->lastRotated)
		return SPIN_NONE;
	static const int CORNERS[4][2] = { {0,0}, {2,0}, {2,2}, {0,2} };
	/* Indices of the two corners on the side the T points at, per rotation. */
	static const int FRONT[4][2] = { {0,1}, {1,2}, {2,3}, {3,0} };
	bool filled[4];
	int count = 0;
	for (int i = 0; i < 4; i++) {
		filled[i] = occupied(g, g->x + CORNERS[i][0], g->y + CORNERS[i][1]);
		count += filled[i];
	}
	if (count < 3)
		return SPIN_NONE;
	return filled[FRONT[g->rot][0]] && filled[FRONT[g->rot][1]] ? SPIN_FULL : SPIN_MINI;
}

static int clear_lines(Game *g)
{
	int cleared = 0;
	g->clearedRows = 0;
	/* Record which rows are full before anything moves. */
	for (int r = 0; r < BOARD_H; r++) {
		bool full = true;
		for (int c = 0; c < BOARD_W; c++)
			if (g->cells[r][c] == CELL_EMPTY) {
				full = false;
				break;
			}
		if (full)
			g->clearedRows |= 1u << r;
	}
	for (int r = BOARD_H - 1; r >= 0; r--) {
		bool full = true;
		for (int c = 0; c < BOARD_W; c++)
			if (g->cells[r][c] == CELL_EMPTY) {
				full = false;
				break;
			}
		if (!full)
			continue;
		memmove(&g->cells[1][0], &g->cells[0][0], (size_t)r * BOARD_W);
		memset(&g->cells[0][0], CELL_EMPTY, BOARD_W);
		cleared++;
		r++; /* re-check the row that just moved down */
	}
	return cleared;
}

static void lock_piece(Game *g)
{
	int spin = detect_spin(g);
	bool aboveVisible = true;
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			if (!piece_cell(g->type, g->rot, r, c))
				continue;
			int by = g->y + r;
			if (by >= 0)
				g->cells[by][g->x + c] = (uint8_t)(g->type + 1);
			if (by >= HIDDEN_H)
				aboveVisible = false;
		}

	int cleared = clear_lines(g);
	g->pieces++;
	g->lockEvent++;
	g->lastClear = cleared;
	g->lastSpin = spin;
	g->lastAttack = 0;
	g->lastAllClear = false;
	if (cleared > 0 || spin != SPIN_NONE)
		g->clearEvent++;

	if (cleared > 0) {
		static const int PTS[5]      = { 0, 100, 300, 500, 800 };
		static const int PTS_SPIN[4] = { 400, 800, 1200, 1600 };
		static const int PTS_MINI[4] = { 100, 200, 400, 400 };
		int lc = cleared > 3 ? 3 : cleared;
		int pts = spin == SPIN_FULL ? PTS_SPIN[lc] : spin == SPIN_MINI ? PTS_MINI[lc] : PTS[cleared];

		bool b2bable = cleared == 4 || spin != SPIN_NONE;
		g->b2b = b2bable ? g->b2b + 1 : -1;
		g->combo++;
		if (g->b2b > 0)
			pts = pts * 3 / 2;
		g->score += (uint32_t)((pts + 50 * g->combo) * (g->level + 1));
		g->lines += cleared;
		g->level = g->lines / 10;

		int attack = spin == SPIN_FULL ? ATTACK_SPIN[lc] : spin == SPIN_MINI ? ATTACK_MINI[lc] : ATTACK[cleared];
		if (g->b2b > 0)
			attack++;
		int ci = g->combo < (int)(sizeof(COMBO_BONUS) / sizeof(COMBO_BONUS[0])) ? g->combo
		         : (int)(sizeof(COMBO_BONUS) / sizeof(COMBO_BONUS[0])) - 1;
		attack += COMBO_BONUS[ci];

		bool empty = true;
		for (int i = 0; i < BOARD_H * BOARD_W; i++)
			if ((&g->cells[0][0])[i] != CELL_EMPTY)
				empty = false;
		if (empty) {
			g->lastAllClear = true;
			attack += ALL_CLEAR_BONUS;
			g->score += 3500u * (uint32_t)(g->level + 1);
		}
		g->lastAttack = attack;

		/* Outgoing attack first cancels our own incoming garbage. */
		int cancel = attack < g->pendingGarbage ? attack : g->pendingGarbage;
		g->pendingGarbage -= cancel;
		g->garbageOut += attack - cancel;
	} else {
		g->combo = -1;
		if (spin != SPIN_NONE)
			g->score += (uint32_t)((spin == SPIN_FULL ? 400 : 100) * (g->level + 1));
		/* Locking entirely in the hidden rows is a lock-out. */
		if (aboveVisible)
			g->over = true;
		push_garbage(g);
	}

	if (!g->over)
		spawn_next(g);
}

static bool try_move(Game *g, int dx, int dy)
{
	if (!fits(g, g->type, g->rot, g->x + dx, g->y + dy))
		return false;
	g->x += dx;
	g->y += dy;
	g->lastRotated = false;
	if (dx)
		g->moveEvent++;
	return true;
}

static void reset_lock(Game *g)
{
	if (g->lockTimer > 0 && g->lockResets < MAX_RESETS) {
		g->lockTimer = 0;
		g->lockResets++;
	}
}

static void try_rotate(Game *g, int dir)
{
	int nr = (g->rot + dir) & 3;
	for (unsigned i = 0; i < sizeof(KICKS) / sizeof(KICKS[0]); i++) {
		int nx = g->x + KICKS[i][0], ny = g->y + KICKS[i][1];
		if (fits(g, g->type, nr, nx, ny)) {
			g->rot = nr;
			g->x = nx;
			g->y = ny;
			g->lastRotated = true;
			g->rotateEvent++;
			reset_lock(g);
			return;
		}
	}
}

int game_ghost_y(const Game *g)
{
	int y = g->y;
	while (fits(g, g->type, g->rot, g->x, y + 1))
		y++;
	return y;
}

static int gravity_frames(const Game *g)
{
	int f = 48 - (g->zen ? 0 : g->level) * 4;
	return f < 2 ? 2 : f;
}

void game_update(Game *g, const GameInput *in)
{
	if (g->over)
		return;

	g->frames++;

	if (in->pressed & IN_HOLD && !g->holdUsed) {
		int cur = g->type;
		if (g->hold < 0) {
			g->hold = cur;
			spawn_next(g);
		} else {
			int t = g->hold;
			g->hold = cur;
			spawn(g, t);
		}
		g->holdUsed = true;
		g->holdEvent++;
		if (g->over)
			return;
	}

	if (in->pressed & IN_ROT_CW)
		try_rotate(g, 1);
	if (in->pressed & IN_ROT_CCW)
		try_rotate(g, -1);

	/* Horizontal movement with delayed auto shift. */
	int dir = 0;
	if (in->held & IN_LEFT)
		dir = -1;
	else if (in->held & IN_RIGHT)
		dir = 1;
	if (dir == 0) {
		g->dasDir = 0;
	} else if (dir != g->dasDir) {
		g->dasDir = dir;
		g->dasTimer = 0;
		if (try_move(g, dir, 0))
			reset_lock(g);
	} else if (++g->dasTimer >= DAS_DELAY &&
	           (g->dasTimer - DAS_DELAY) % DAS_REPEAT == 0) {
		if (try_move(g, dir, 0))
			reset_lock(g);
	}

	if (in->pressed & IN_HARD) {
		int dropped = 0;
		while (try_move(g, 0, 1))
			dropped++;
		g->score += (uint32_t)(dropped * 2);
		g->hardDropEvent++;
		lock_piece(g);
		return;
	}

	int grav = gravity_frames(g);
	if (in->held & IN_DOWN && grav > 2)
		grav = 2;
	if (++g->gravityTimer >= grav) {
		g->gravityTimer = 0;
		if (try_move(g, 0, 1)) {
			if (in->held & IN_DOWN)
				g->score += 1;
		}
	}

	if (!fits(g, g->type, g->rot, g->x, g->y + 1)) {
		if (++g->lockTimer >= LOCK_DELAY)
			lock_piece(g);
	} else {
		g->lockTimer = 0;
	}
}
