#ifndef FONT_H
#define FONT_H

#include <stdint.h>

typedef struct {
	uint32_t off;      /* offset into the bitmap array */
	uint8_t  w, h;
	int8_t   xo, yo;   /* offset from pen position / line top */
	uint8_t  adv;
} Glyph;

typedef struct {
	const Glyph   *glyphs;  /* ASCII 32..126 */
	const uint8_t *bits;    /* 8-bit alpha */
	int capTop, capH, lineH;
} Font;

extern const Font FONT_SMALL, FONT_MED, FONT_BIG, FONT_HUGE;

#endif
