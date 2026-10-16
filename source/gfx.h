#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include "font.h"

/* A drawing surface. Pixel (x, y) lives at base + x*xs + y*ys, stored B,G,R.
 * This lets the same code draw into the 3DS's rotated framebuffers and into
 * an ordinary row-major buffer for host-side previews. */
typedef struct {
	uint8_t *base;
	int xs, ys;
	int w, h;
} Canvas;

enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

void cv_init_rotated(Canvas *cv, uint8_t *fb, int w, int h);
void cv_init_linear(Canvas *cv, uint8_t *buf, int w, int h);

void cv_fill(Canvas *cv, int x, int y, int w, int h, uint32_t rgb);
void cv_blend(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha);
void cv_frame(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha);
void cv_vgradient(Canvas *cv, int x, int y, int w, int h, uint32_t top, uint32_t bottom);

/* y is the top of the capital letters, so mixed sizes line up predictably. */
int  text_width(const Font *f, const char *s, int spacing);
void cv_text(Canvas *cv, const Font *f, int x, int y, const char *s,
             uint32_t rgb, int alpha, int spacing, int align);

uint32_t rgb_mix(uint32_t a, uint32_t b, int t); /* t = 0..255 towards b */

#endif
