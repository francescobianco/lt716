/* Minimal ST7735 driver for the LT716 TFT (hardware SPI, RGB565). */
#ifndef ST7735_H
#define ST7735_H

#include "tl_common.h"

/* LCD wiring on the LT715/LT716 board (FitPro-LT715-TLSR8232 docs/hardware.md) */
#define LCD_MOSI GPIO_PC3
#define LCD_CLK  GPIO_PC5
#define LCD_DC   GPIO_PC1
#define LCD_RST  GPIO_PA6
#define LCD_CS   GPIO_PA1
#define LCD_BL   GPIO_PB3 /* high = on */

/* MADCTL bits */
#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_BGR 0x08

typedef struct {
    u8 colstart; /* offset of column 0 in the controller RAM */
    u8 rowstart; /* offset of row 0 */
    u8 width;
    u8 height;
    u8 madctl;   /* rotation / mirroring / colour order */
    u8 invert;   /* 1 = INVON (most IPS panels), 0 = INVOFF */
} lcd_cfg_t;

extern lcd_cfg_t lcd;

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BLACK  0x0000
#define C_WHITE  0xFFFF
#define C_RED    0xF800
#define C_GREEN  0x07E0
#define C_BLUE   0x001F
#define C_YELLOW 0xFFE0
#define C_GREY   0x4208

void lcd_init(void);           /* reset + init sequence + apply lcd */
void lcd_apply(void);          /* MADCTL and inversion from lcd */
void lcd_backlight(int on);
void lcd_fill(int x, int y, int w, int h, u16 color);

#endif
