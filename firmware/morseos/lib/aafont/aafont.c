/* MorseOS antialiased text, see aafont.h. */
#include "aafont.h"
#include "st7735.h"
#include "aafont_data.h"

static int glyph(const aafont_t *f, char c) {
    int i;
    if (c >= 'a' && c <= 'z') c -= 32;
    for (i = 0; f->chars[i]; i++)
        if (f->chars[i] == c) return i;
    for (i = 0; f->chars[i]; i++)
        if (f->chars[i] == '?') return i;
    return 0;
}

int aafont_width(const aafont_t *f, const char *s) {
    int w = 0;
    for (; *s; s++) w += f->w[glyph(f, *s)];
    return w;
}

int aafont_text(const aafont_t *f, int x, int y, const char *s, u16 fg, u16 bg) {
    for (; *s; s++) {
        int g = glyph(f, *s);
        lcd_blit_rle(x, y, f->w[g], f->h, f->data + f->off[g], fg, bg);
        x += f->w[g];
    }
    return x;
}

void aafont_line(const aafont_t *f, int y, const char *s, u16 fg, u16 bg) {
    int w = aafont_width(f, s), x = (lcd.width - w) / 2;
    if (x < 0) x = 0;
    lcd_fill(0, y, x, f->h, bg);
    x = aafont_text(f, x, y, s, fg, bg);
    lcd_fill(x, y, lcd.width - x, f->h, bg);
}
