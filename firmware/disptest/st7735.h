/* Minimal ST7735 driver for the LT716 TFT (hardware SPI, RGB565). */
#ifndef ST7735_H
#define ST7735_H

#include "tl_common.h"

/* LCD wiring on the LT715/LT716 board (FitPro-LT715-TLSR8232 docs/hardware.md) */
#define LCD_MOSI GPIO_PC3
#define LCD_CLK  GPIO_PC5
/* DC, CS, RST, backlight and panel power can be changed at run time (BLE command 54):
 * second LT716 (2026-11-23): GC9107, power PA0, DC PC6, backlight PA2 (high = on). 0 = none. */
typedef struct { u16 dc, cs, rst, bl, pwr; } lcd_pins_t;
extern lcd_pins_t lcd_pins;
#define LCD_DC   lcd_pins.dc
#define LCD_RST  lcd_pins.rst
#define LCD_CS   lcd_pins.cs
#define LCD_BL   lcd_pins.bl  /* high = on */
#define LCD_PWR  lcd_pins.pwr /* high = on */

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

#define LCD_VARIANTS 7
#define LCD_AUTO     0xFF
extern u8 lcd_variant;  /* 0 our ST7735, 1 stock ST7735S, 2 GC9106, 3 GC9107, 4 NV 333023,
                           5 NV 333025, 6 GC9107 of V10712, LCD_AUTO = from the panel ID like the stock firmware */
extern u8 lcd_id[3];    /* RDDID bytes read by the last lcd_init() */
extern u8 lcd_chosen;   /* variant used by the last lcd_init() */
void lcd_init(void);           /* reset, read ID, init sequence; sets lcd for that variant */
void lcd_apply(void);          /* MADCTL and inversion from lcd */
void lcd_backlight(int on);
void lcd_cmd(u8 c, const u8 *a, int n);   /* raw panel command with its data */
void lcd_fill(int x, int y, int w, int h, u16 color);

#endif
