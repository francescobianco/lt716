/* MorseOS antialiased text: proportional Ubuntu Condensed Bold, the face of the clock digits. */
#ifndef AAFONT_H
#define AAFONT_H

#include "tl_common.h"

typedef struct {
    u8 h;               /* cell height: top of the capitals to the baseline */
    const char *chars;  /* characters in the font, upper case */
    const u8 *w;        /* advance of each glyph */
    const u16 *off;     /* its run-length code in data */
    const u8 *data;
} aafont_t;

extern const aafont_t aafont_medium;  /* 20 px: date, typed letters (cells 14 px high) */
extern const aafont_t aafont_small;   /* 10 px: top bar */

int aafont_width(const aafont_t *f, const char *s);
/* draw s with its top left at (x, y), fg on bg; lower case drawn as upper case, unknown
 * characters as '?'. Returns the x after it. */
int aafont_text(const aafont_t *f, int x, int y, const char *s, u16 fg, u16 bg);
/* centred in the full width; the rest of the line (h rows at y) is filled with bg */
void aafont_line(const aafont_t *f, int y, const char *s, u16 fg, u16 bg);

#endif
