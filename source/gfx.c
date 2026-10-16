#include "gfx.h"

void cv_init_rotated(Canvas *cv, uint8_t *fb, int w, int h)
{
	/* 3DS framebuffers are stored as columns of `h` pixels, bottom to top. */
	cv->base = fb + (h - 1) * 3;
	cv->xs = h * 3;
	cv->ys = -3;
	cv->w = w;
	cv->h = h;
}

void cv_init_linear(Canvas *cv, uint8_t *buf, int w, int h)
{
	cv->base = buf;
	cv->xs = 3;
	cv->ys = w * 3;
	cv->w = w;
	cv->h = h;
}

static int clip(const Canvas *cv, int *x, int *y, int *w, int *h)
{
	if (*x < 0) { *w += *x; *x = 0; }
	if (*y < 0) { *h += *y; *y = 0; }
	if (*x + *w > cv->w) *w = cv->w - *x;
	if (*y + *h > cv->h) *h = cv->h - *y;
	return *w > 0 && *h > 0;
}

void cv_fill(Canvas *cv, int x, int y, int w, int h, uint32_t rgb)
{
	if (!clip(cv, &x, &y, &w, &h))
		return;
	uint8_t r = rgb >> 16, g = rgb >> 8, b = rgb;
	for (int i = 0; i < w; i++) {
		uint8_t *p = cv->base + (x + i) * cv->xs + y * cv->ys;
		for (int j = 0; j < h; j++, p += cv->ys) {
			p[0] = b; p[1] = g; p[2] = r;
		}
	}
}

static inline void blend_px(uint8_t *p, int r, int g, int b, int a)
{
	p[0] = (uint8_t)(p[0] + (((b - p[0]) * a) >> 8));
	p[1] = (uint8_t)(p[1] + (((g - p[1]) * a) >> 8));
	p[2] = (uint8_t)(p[2] + (((r - p[2]) * a) >> 8));
}

void cv_blend(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	if (alpha <= 0 || !clip(cv, &x, &y, &w, &h))
		return;
	if (alpha >= 255) {
		cv_fill(cv, x, y, w, h, rgb);
		return;
	}
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
	for (int i = 0; i < w; i++) {
		uint8_t *p = cv->base + (x + i) * cv->xs + y * cv->ys;
		for (int j = 0; j < h; j++, p += cv->ys)
			blend_px(p, r, g, b, alpha);
	}
}

void cv_frame(Canvas *cv, int x, int y, int w, int h, uint32_t rgb, int alpha)
{
	cv_blend(cv, x, y, w, 1, rgb, alpha);
	cv_blend(cv, x, y + h - 1, w, 1, rgb, alpha);
	cv_blend(cv, x, y + 1, 1, h - 2, rgb, alpha);
	cv_blend(cv, x + w - 1, y + 1, 1, h - 2, rgb, alpha);
}

uint32_t rgb_mix(uint32_t a, uint32_t b, int t)
{
	uint32_t out = 0;
	for (int s = 0; s <= 16; s += 8) {
		int ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
		out |= (uint32_t)(ca + ((cb - ca) * t) / 255) << s;
	}
	return out;
}

void cv_vgradient(Canvas *cv, int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
	for (int j = 0; j < h; j++)
		cv_fill(cv, x, y + j, w, 1, rgb_mix(top, bottom, h > 1 ? j * 255 / (h - 1) : 0));
}

static const Glyph *glyph(const Font *f, char c)
{
	if (c < 32 || c > 126)
		c = '?';
	return &f->glyphs[c - 32];
}

int text_width(const Font *f, const char *s, int spacing)
{
	int w = 0;
	for (; *s; s++)
		w += glyph(f, *s)->adv + (s[1] ? spacing : 0);
	return w;
}

void cv_text(Canvas *cv, const Font *f, int x, int y, const char *s,
             uint32_t rgb, int alpha, int spacing, int align)
{
	if (align == ALIGN_CENTER)
		x -= text_width(f, s, spacing) / 2;
	else if (align == ALIGN_RIGHT)
		x -= text_width(f, s, spacing);
	int top = y - f->capTop;
	int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;

	for (; *s; s++) {
		const Glyph *gl = glyph(f, *s);
		const uint8_t *src = f->bits + gl->off;
		for (int j = 0; j < gl->h; j++) {
			int py = top + gl->yo + j;
			if (py < 0 || py >= cv->h)
				continue;
			for (int i = 0; i < gl->w; i++) {
				int px = x + gl->xo + i;
				int a = src[j * gl->w + i];
				if (!a || px < 0 || px >= cv->w)
					continue;
				blend_px(cv->base + px * cv->xs + py * cv->ys, r, g, b, (a * alpha) >> 8);
			}
		}
		x += gl->adv + spacing;
	}
}
