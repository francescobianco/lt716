/* 5x7 bitmap font for MorseOS: digits, A-Z and a few symbols. */
#ifndef FONT_H
#define FONT_H

#include "tl_common.h"

/* Draw text at (x, y), each font pixel a scale x scale block; fg on bg. Returns the x after it. */
int font_text(int x, int y, const char *s, int scale, u16 fg, u16 bg);
/* Width in pixels of text at a scale (6 columns per character, the last spacing included). */
int font_width(const char *s, int scale);

#endif
