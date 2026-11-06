/*
 * Minimal ST7735(S) driver for the LT716: hardware SPI master at 8 MHz, RGB565.
 * Init values are the Sitronix datasheet defaults used by most ST7735 drivers.
 */
#include "st7735.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"
#include "drivers/5316/register.h"
#include "drivers/5316/spi.h"
#include "drivers/5316/timer.h"

/* 0.96" 80x160 panels sit at an offset in the 132x162 controller RAM. */
/* Found on the LT716 0.96" panel with firmware/disptest (2026-11-06): full border visible,
 * colours right, key at the bottom as in the stock firmware. */
lcd_cfg_t lcd = {26, 1, 80, 160, MADCTL_MY | MADCTL_MX | MADCTL_BGR, 1};

static inline void spi_byte(u8 b) {
    reg_spi_data = b;
    while (reg_spi_ctrl & FLD_SPI_BUSY);
}

static void cmd(u8 c) {
    gpio_write(LCD_DC, 0);
    spi_byte(c);
    gpio_write(LCD_DC, 1);
}

static void cmd_args(u8 c, const u8 *a, int n) {
    gpio_write(LCD_CS, 0);
    cmd(c);
    while (n--) spi_byte(*a++);
    gpio_write(LCD_CS, 1);
}

static void out_pin(u32 pin, int level) {
    gpio_set_func(pin, AS_GPIO);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 1);
    gpio_write(pin, level);
}

void lcd_backlight(int on) { gpio_write(LCD_BL, on ? 1 : 0); }

void lcd_apply(void) {
    u8 m = lcd.madctl;
    cmd_args(0x36, &m, 1);                      /* MADCTL */
    cmd_args(lcd.invert ? 0x21 : 0x20, 0, 0);   /* INVON / INVOFF */
}

void lcd_init(void) {
    static const u8 frm[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const u8 pw1[] = {0xA2, 0x02, 0x84}, pw2[] = {0xC5}, pw3[] = {0x0A, 0x00},
                    pw4[] = {0x8A, 0x2A}, pw5[] = {0x8A, 0xEE}, vm[] = {0x0E}, inv[] = {0x07},
                    col[] = {0x05};
    static const u8 gp[] = {0x02, 0x1c, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2d,
                            0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    static const u8 gn[] = {0x03, 0x1d, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                            0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};

    gpio_set_func(LCD_MOSI, AS_SPI_MDO);
    gpio_set_func(LCD_CLK, AS_SPI_MCK);
    gpio_set_output_en(LCD_MOSI, 1);
    gpio_set_output_en(LCD_CLK, 1);
    out_pin(LCD_CS, 1);
    out_pin(LCD_DC, 1);
    out_pin(LCD_RST, 1);
    out_pin(LCD_BL, 0);
    spi_master_init(0, SPI_MODE0); /* 16 MHz / 2 = 8 MHz */

    gpio_write(LCD_RST, 0);
    sleep_us(10 * 1000);
    gpio_write(LCD_RST, 1);
    sleep_us(120 * 1000);

    cmd_args(0x01, 0, 0);          /* SWRESET */
    sleep_us(150 * 1000);
    cmd_args(0x11, 0, 0);          /* SLPOUT */
    sleep_us(255 * 1000);
    cmd_args(0xB1, frm, 3);        /* FRMCTR1..3 */
    cmd_args(0xB2, frm, 3);
    cmd_args(0xB3, frm, 6);
    cmd_args(0xB4, inv, 1);        /* INVCTR */
    cmd_args(0xC0, pw1, 3);        /* PWCTR1..5 */
    cmd_args(0xC1, pw2, 1);
    cmd_args(0xC2, pw3, 2);
    cmd_args(0xC3, pw4, 2);
    cmd_args(0xC4, pw5, 2);
    cmd_args(0xC5, vm, 1);         /* VMCTR1 */
    cmd_args(0x3A, col, 1);        /* COLMOD: 16 bit */
    cmd_args(0xE0, gp, 16);        /* gamma */
    cmd_args(0xE1, gn, 16);
    lcd_apply();
    cmd_args(0x13, 0, 0);          /* NORON */
    sleep_us(10 * 1000);
    cmd_args(0x29, 0, 0);          /* DISPON */
    sleep_us(100 * 1000);
}

void lcd_fill(int x, int y, int w, int h, u16 color) {
    u8 ca[4], ra[4];
    int x0, y0, x1, y1;
    u32 n;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > lcd.width) w = lcd.width - x;
    if (y + h > lcd.height) h = lcd.height - y;
    if (w <= 0 || h <= 0) return;
    x0 = x + lcd.colstart;
    x1 = x0 + w - 1;
    y0 = y + lcd.rowstart;
    y1 = y0 + h - 1;
    ca[0] = x0 >> 8; ca[1] = x0; ca[2] = x1 >> 8; ca[3] = x1;
    ra[0] = y0 >> 8; ra[1] = y0; ra[2] = y1 >> 8; ra[3] = y1;
    cmd_args(0x2A, ca, 4);         /* CASET */
    cmd_args(0x2B, ra, 4);         /* RASET */
    gpio_write(LCD_CS, 0);
    cmd(0x2C);                     /* RAMWR */
    for (n = (u32)w * h; n; n--) {
        spi_byte(color >> 8);
        spi_byte(color);
    }
    gpio_write(LCD_CS, 1);
}
