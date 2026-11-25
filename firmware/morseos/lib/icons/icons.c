/* MorseOS menu icons: the main menu shows these instead of the labels. */
#include "icons.h"
#include "st7735.h"
#include "icons_data.h"

const u8 *icon_for(const char *label) {
    unsigned i;
    for (i = 0; i < sizeof(icon_table) / sizeof(icon_table[0]); i++) {
        const char *a = icon_table[i].label, *b = label;
        while (*a && *a == *b) a++, b++;
        if (*a == *b) return icon_table[i].bits;
    }
    return 0;
}

const u8 *icon_back(void) { return icon_back_bits; }

void icon_draw(int x, int y, const u8 *icon, u16 fg, u16 bg) {
    lcd_blit4(x, y, ICON_SIZE, ICON_SIZE, icon, fg, bg);
}
