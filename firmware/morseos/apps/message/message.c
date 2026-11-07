/* MESSAGGIO: a notification, wrapped to 13 columns. Any touch: back to the watch face. */
#include "mos.h"
#include "message.h"
#include "font.h"
#include "st7735.h"
#include "drivers/5316/clock.h"

static char text[200];

static void draw(void) {
    int y = 16, i = 0, col;
    char ln[14];
    lcd_fill(0, 0, lcd.width, lcd.height, C_BLACK);
    mos_center_text(3, "MESSAGGIO", 1, C_YELLOW);
    while (text[i] && y < lcd.height - 16) {
        for (col = 0; col < 13 && text[i]; col++) {
            char c = text[i++];
            ln[col] = (c >= 32 && c < 127) ? c : '?';
        }
        ln[col] = 0;
        font_text(1, y, ln, 1, C_WHITE, C_BLACK);
        y += 10;
    }
}

static void key(int ev) { mos_home(); }

const mos_app_t app_message = {"message", 0, 10, 0, draw, 0, key, 0, 0, 0, 0};

void message_show(const char *t) {
    int i;
    for (i = 0; t[i] && i < (int)sizeof(text) - 1; i++) text[i] = t[i];
    text[i] = 0;
    mos_open(&app_message);  /* drawn first (in the dark if the screen is off), then lit */
    mos_wake();
    mos_buzz(150);
    sleep_us(120 * 1000);
    mos_buzz(150);
}
