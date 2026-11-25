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
lcd_pins_t lcd_pins;
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
    if (!pin) return;
    gpio_set_func(pin, AS_GPIO);
    gpio_set_input_en(pin, 0);
    gpio_set_output_en(pin, 1);
    gpio_write(pin, level);
}

static void release_pin(u32 pin) {
    if (!pin) return;
    gpio_set_output_en(pin, 0);
    gpio_set_input_en(pin, 1);
}

/* ---- boards: the LT716 units we know differ only in the display wiring ----
 * The signature is the panel itself: with a board's pins, RDDID must return its controller ID.
 * Auto detection (LT716_BAND 0) tries the boards in order and keeps the first that answers;
 * the last one is the fallback and accepts any ID. Band 2 is probed first because its pins
 * (PA0, PC6, PA2) were driven on band 1 for minutes on 2026-11-24 (band 2 battmon) without
 * harm, while band 1's (PC1, PB3) were never tried on band 2: there they are never touched. */
static const lcd_board_t boards[] = {
    {2, "GC9107", {GPIO_PC6, GPIO_PA1, GPIO_PA6, GPIO_PA2, GPIO_PA0}, 1, {0x00, 0x91, 0x07}},
    {1, "ST7735", {GPIO_PC1, GPIO_PA1, GPIO_PA6, GPIO_PB3, 0}, 0, {0, 0, 0}},
};
#define BOARDS (sizeof(boards) / sizeof(boards[0]))
#ifndef LT716_BAND
#define LT716_BAND 0
#endif
const lcd_board_t *lcd_board;  /* set by the first lcd_init() */

void lcd_backlight(int on) { if (LCD_BL) gpio_write(LCD_BL, on ? 1 : 0); }

void lcd_sleep(int sleep) {
    static int asleep;
    if (sleep == asleep) return;
    asleep = sleep;
    cmd_args(sleep ? 0x10 : 0x11, 0, 0);   /* SLPIN / SLPOUT */
    sleep_us((sleep ? 5 : 10) * 1000);     /* SLPOUT: oscillator and boosters back */
}

void lcd_apply(void) {
    u8 m = lcd.madctl;
    cmd_args(0x36, &m, 1);                      /* MADCTL */
    cmd_args(lcd.invert ? 0x21 : 0x20, 0, 0);   /* INVON / INVOFF */
}

/* ---- panel identification and init scripts, as in the stock FitPro firmware ----
 * The stock images (V17899, V08928) read the controller ID with RDDID (0x04) over
 * bit-banged 3-wire SPI and pick one of several init sequences. Script format:
 * <cmd> <n> <n data bytes>, with n = DLY meaning "wait <next byte> x 10 ms", END to stop. */
#define DLY 0xFE
#define END 0xFF, 0xFF

/* 0: our original ST7735 sequence (handled in code) */
static const u8 s_st7735s[] = {           /* 1: stock ST7735S (V17899 0x5DE0) */
    0x11, 0, 0, DLY, 12, 0x21, 0,
    0xB1, 3, 0x05, 0x3A, 0x3A, 0xB2, 3, 0x05, 0x3A, 0x3A,
    0xB3, 6, 0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A, 0xB4, 1, 0x03,
    0xC0, 3, 0x62, 0x02, 0x04, 0xC1, 1, 0xC0, 0xC2, 2, 0x0D, 0x00, 0xC3, 2, 0x8D, 0x6A,
    0xC4, 2, 0x8D, 0xEE, 0xC5, 1, 0x0E,
    0xE0, 16, 0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07, 0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10,
    0xE1, 16, 0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08, 0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10,
    0x3A, 1, 0x05, 0x36, 1, 0xC8, 0x29, 0, END};
static const u8 s_gc9106[] = {            /* 2: GC9106 type (V17899 0x5698) */
    0x11, 0, 0, DLY, 12, 0xFE, 0, 0xEF, 0,
    0xB3, 1, 0x2B, 0x36, 1, 0xD8, 0x3A, 1, 0x05, 0xB5, 1, 0x01, 0xB6, 1, 0x11, 0xAC, 1, 0x0B,
    0xB4, 1, 0x21, 0xB1, 1, 0xC8, 0xC0, 1, 0xC3, 0xC6, 2, 0x1F, 0x00, 0xF8, 2, 0x80, 0x06,
    0xF3, 2, 0x01, 0x03, 0xF5, 2, 0x48, 0x90, 0xB2, 1, 0x0D, 0xEA, 1, 0x65, 0xEB, 2, 0x75, 0x66,
    0xB0, 1, 0x3D, 0xC2, 1, 0x0D, 0xC3, 1, 0x4C, 0xC4, 1, 0x10, 0xC5, 1, 0x10,
    0xE6, 2, 0x40, 0x27, 0xE7, 2, 0x60, 0x07, 0xA3, 1, 0x12,
    0xF0, 14, 0x17, 0x39, 0x1B, 0x4E, 0x95, 0x32, 0x33, 0x00, 0x12, 0x0A, 0x09, 0x12, 0x12, 0x0F,
    0xF1, 14, 0x17, 0x39, 0x1B, 0x4E, 0x65, 0x32, 0x33, 0x00, 0x12, 0x0A, 0x09, 0x0E, 0x0C, 0x0F,
    0xFE, 0, 0xFF, 0, 0x11, 0, 0, DLY, 12, 0x29, 0, END};
static const u8 s_gc9107[] = {            /* 3: GC9107 type (V17899 0x58D8) */
    0x11, 0, 0, DLY, 12, 0xFE, 0, 0xEF, 0,
    0xB0, 1, 0xC0, 0xB2, 1, 0x25, 0xB3, 1, 0x03, 0xB7, 1, 0x01, 0xB6, 1, 0x19, 0xAC, 1, 0xCB,
    0xAB, 1, 0x0F, 0x3A, 1, 0x05, 0xB4, 1, 0x04, 0xA8, 1, 0x0C, 0xB8, 1, 0x08, 0xEA, 1, 0x4F,
    0xE8, 1, 0x2C, 0xC6, 1, 0x2A, 0xC7, 1, 0x1F,
    0xF0, 14, 0x09, 0x29, 0x08, 0x39, 0xA4, 0x1F, 0x13, 0x60, 0x00, 0x08, 0x08, 0x16, 0x10, 0x1F,
    0xF1, 14, 0x17, 0x2A, 0x58, 0x3E, 0xD6, 0x16, 0x3F, 0x60, 0x04, 0x06, 0x0D, 0x1F, 0x1C, 0x10,
    0x00, DLY, 12, 0x11, 0, 0, DLY, 12, 0x29, 0, 0, DLY, 12, END};
static const u8 s_nv333023[] = {          /* 4: "FF A5" type, ID 33 30 23 (V17899 0x5A80) */
    0x11, 0, 0, DLY, 12, 0x20, 0, 0xFF, 1, 0xA5, 0x3A, 1, 0x65, 0x51, 1, 0x14, 0x53, 1, 0x11,
    0x62, 1, 0x10, 0x86, 1, 0x00, 0x87, 1, 0x1A, 0x88, 1, 0x0E, 0x89, 1, 0x18, 0x61, 1, 0x10,
    0x93, 1, 0x12, 0x94, 1, 0x10, 0x95, 1, 0x10, 0x96, 1, 0x0E, 0xB2, 1, 0x0F, 0xB4, 1, 0x60,
    0x91, 1, 0x10, 0xC1, 1, 0xF1, 0xC5, 1, 0xF8, 0xB5, 1, 0x00, 0xC3, 1, 0x11, 0x83, 1, 0x10,
    0x2A, 4, 0x00, 0x84, 0x00, 0x84, 0x2B, 4, 0x00, 0x00, 0x00, 0x1F,
    0x2C, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x11, 0x00, 0x0B, 0x00, 0x18, 0x00, 0x05, 0x00, 0x0D, 0x00, 0x0E, 0x00, 0x0A, 0x00, 0x1E, 0x00,
    0x18, 0x00, 0x1A, 0x00, 0x1E,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0x0E, 0x00, 0x06, 0x00, 0x13, 0x00, 0x01, 0x00, 0x06, 0x00, 0x09, 0x00, 0x00, 0x00, 0x15, 0x00,
    0x10, 0x00, 0x0E, 0x00, 0x1A,
    0x2A, 4, 0x00, 0x00, 0x00, 0x7F, 0x2B, 4, 0x00, 0x00, 0x00, 0x9F, 0x83, 1, 0x00, 0xFF, 1, 0x00,
    0x11, 0, 0, DLY, 20, 0x21, 0, 0x36, 1, 0x00, 0x29, 0, END};
static const u8 s_nv333025[] = {          /* 5: "FF A5" type, ID 33 30 25 (V08928 0x60B0) */
    0xFF, 1, 0xA5, 0x3E, 1, 0x09, 0x3A, 1, 0x65, 0x82, 1, 0x00, 0x63, 1, 0x0F, 0x64, 1, 0x0F,
    0xB4, 1, 0x34, 0xB5, 1, 0x30, 0x83, 1, 0x03, 0x86, 1, 0x04, 0x87, 1, 0x16, 0x88, 1, 0x0A,
    0x89, 1, 0x2E, 0x93, 1, 0x63, 0x96, 1, 0x81, 0xC3, 1, 0x10, 0xE6, 1, 0x00, 0x99, 1, 0x01,
    0x70, 1, 0x02, 0x71, 1, 0x10, 0x72, 1, 0x18, 0x73, 1, 0x0A, 0x74, 1, 0x13, 0x75, 1, 0x19,
    0x76, 1, 0x1A, 0x77, 1, 0x09, 0x78, 1, 0x08, 0x79, 1, 0x1F, 0x7A, 1, 0x05, 0x7B, 1, 0x0A,
    0x7C, 1, 0x0E, 0x7D, 1, 0x0A, 0x7E, 1, 0x10, 0x7F, 1, 0x02,
    0xA0, 1, 0x06, 0xA1, 1, 0x2C, 0xA2, 1, 0x0C, 0xA3, 1, 0x0F, 0xA4, 1, 0x0B, 0xA5, 1, 0x23,
    0xA6, 1, 0x45, 0xA7, 1, 0x07, 0xA8, 1, 0x0A, 0xA9, 1, 0x3B, 0xAA, 1, 0x0D, 0xAB, 1, 0x16,
    0xAC, 1, 0x0D, 0xAD, 1, 0x07, 0xAE, 1, 0x19, 0xAF, 1, 0x01,
    0xFF, 1, 0x00, 0x11, 0, 0, DLY, 12, 0x36, 1, 0x88, 0x29, 0, 0, DLY, 1, END};

/* per variant: script, colstart, rowstart, madctl, invert (stock values) */
static const struct { const u8 *s; u8 col, row, madctl, inv; } variants[] = {
    {0, 26, 1, 0xC8, 1},
    {s_st7735s, 26, 1, 0xC8, 1},
    {s_gc9106, 24, 0, 0xD8, 0},
    {s_gc9107, 24, 0, 0x00, 0},  /* second band: key at the bottom, RGB order */
    {s_nv333023, 26, 1, 0x00, 1},
    {s_nv333025, 26, 1, 0x88, 0},
};

u8 lcd_variant = LCD_AUTO;  /* LCD_AUTO = choose from the ID, like the stock firmware */
u8 lcd_id[3];
u8 lcd_chosen;

static void run_script(const u8 *p) {
    while (!(p[0] == 0xFF && p[1] == 0xFF)) {
        if (p[1] == DLY) {
            sleep_us(p[2] * 10000);
            p += 3;
            continue;
        }
        cmd_args(p[0], p + 2, p[1]);
        p += 2 + p[1];
    }
}

static void hw_setup(void) {
    gpio_set_func(LCD_MOSI, AS_SPI_MDO);
    gpio_set_func(LCD_CLK, AS_SPI_MCK);
    gpio_set_input_en(LCD_MOSI, 0);
    gpio_set_output_en(LCD_MOSI, 1);
    gpio_set_output_en(LCD_CLK, 1);
    out_pin(LCD_CS, 1);
    out_pin(LCD_DC, 1);
    out_pin(LCD_RST, 1);
    spi_master_init(0, SPI_MODE0); /* 16 MHz / 2 = 8 MHz */
}

/* RDDID over bit-banged 3-wire SPI, as the stock firmware does: command 04 MSB first,
 * then SDA (PC3) turns into an input, one dummy clock, 24 bits sampled after the rising edge. */
static void read_id(void) {
    int i;
    u32 v = 0;
    out_pin(LCD_CLK, 0);
    out_pin(LCD_MOSI, 0);
    gpio_write(LCD_CS, 0);
    gpio_write(LCD_DC, 0);
    for (i = 7; i >= 0; i--) {
        gpio_write(LCD_MOSI, (0x04 >> i) & 1);
        sleep_us(10);
        gpio_write(LCD_CLK, 1);
        sleep_us(10);
        gpio_write(LCD_CLK, 0);
    }
    gpio_write(LCD_DC, 1);
    gpio_set_output_en(LCD_MOSI, 0);
    gpio_set_input_en(LCD_MOSI, 1);
    sleep_us(10);
    gpio_write(LCD_CLK, 1);   /* dummy clock */
    sleep_us(10);
    gpio_write(LCD_CLK, 0);
    for (i = 0; i < 24; i++) {
        sleep_us(10);
        gpio_write(LCD_CLK, 1);
        sleep_us(5);
        v = v << 1 | (gpio_read(LCD_MOSI) ? 1 : 0);
        sleep_us(5);
        gpio_write(LCD_CLK, 0);
    }
    gpio_write(LCD_CS, 1);
    lcd_id[0] = v >> 16;
    lcd_id[1] = v >> 8;
    lcd_id[2] = v;
}

static u8 pick_variant(void) {
    u8 a = lcd_id[0], b = lcd_id[1], c = lcd_id[2];
    if ((b == 0x89 && c == 0xF0 && (a & 0x7F) == 0x7C) || (a == 0x1C && b == 0x80 && c == 0xF2) ||
        (a == 0x86 && b == 0xEC && c == 0x1E) || (a == 0x83 && b == 0x76 && c == 0x0F))
        return 1;
    if (b == 0x91 && (c == 0x08 || c == 0x06)) return 2;
    if (a == 0x00 && b == 0x91 && c == 0x07) return 3;
    if (a == 0x33 && b == 0x30 && c == 0x23) return 4;
    if (a == 0x33 && b == 0x30 && c == 0x25) return 5;
    return 0;  /* unknown: our original ST7735 sequence */
}

static void reset_and_read_id(void) {
    out_pin(LCD_BL, 0);
    /* real power cycle: registers left wrong (e.g. by bytes taken as commands) do not survive
     * an MCU reboot otherwise, since a floating PA0 keeps the panel half powered */
    out_pin(LCD_PWR, 0);
    sleep_us(100 * 1000);
    out_pin(LCD_PWR, 1);
    sleep_us(20 * 1000);
    hw_setup();
    gpio_write(LCD_RST, 0);        /* stock: 100 ms low, 100 ms high */
    sleep_us(100 * 1000);
    gpio_write(LCD_RST, 1);
    sleep_us(100 * 1000);
    read_id();
    hw_setup();                    /* back to hardware SPI */
}

static void use_board(const lcd_board_t *b) {
    lcd_board = b;
    lcd_pins = b->pins;
}

static void detect_board(void) {
    u32 i;
    for (i = 0; i < BOARDS; i++)
        if (boards[i].band == LT716_BAND) {   /* fixed at build time: no probing */
            use_board(&boards[i]);
            reset_and_read_id();
            return;
        }
    for (i = 0; i + 1 < BOARDS; i++) {
        const lcd_board_t *b = &boards[i];
        use_board(b);
        reset_and_read_id();
        if (b->has_id && lcd_id[0] == b->id[0] && lcd_id[1] == b->id[1] && lcd_id[2] == b->id[2])
            return;
        /* not this board: leave its pins as inputs before trying the next one */
        out_pin(LCD_PWR, 0);
        release_pin(LCD_DC);
        release_pin(LCD_BL);
        release_pin(LCD_PWR);
    }
    use_board(&boards[BOARDS - 1]);
    reset_and_read_id();
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

    if (!lcd_board) {
        detect_board();            /* leaves the panel reset, with lcd_id read */
    } else {
        reset_and_read_id();
    }
    lcd_chosen = lcd_variant < LCD_VARIANTS ? lcd_variant : pick_variant();

    if (lcd_chosen) {
        cmd_args(0x01, 0, 0);      /* SWRESET: the stock scripts rely on RST alone */
        sleep_us(120 * 1000);
        lcd.colstart = variants[lcd_chosen].col;
        lcd.rowstart = variants[lcd_chosen].row;
        lcd.madctl = variants[lcd_chosen].madctl;
        lcd.invert = variants[lcd_chosen].inv;
        run_script(variants[lcd_chosen].s);
        return;
    }
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

/* clip to the screen, set the CASET/RASET window and start RAMWR (CS left low); 0 = nothing */
static int window(int *px, int *py, int *pw, int *ph) {
    u8 ca[4], ra[4];
    int x = *px, y = *py, w = *pw, h = *ph, x0, y0, x1, y1;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > lcd.width) w = lcd.width - x;
    if (y + h > lcd.height) h = lcd.height - y;
    if (w <= 0 || h <= 0) return 0;
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
    *px = x; *py = y; *pw = w; *ph = h;
    return 1;
}

void lcd_fill(int x, int y, int w, int h, u16 color) {
    u32 n;
    if (!window(&x, &y, &w, &h)) return;
    for (n = (u32)w * h; n; n--) {
        spi_byte(color >> 8);
        spi_byte(color);
    }
    gpio_write(LCD_CS, 1);
}

static u16 blend(u16 fg, u16 bg, int a) {  /* a = 0..15 */
    int r = ((bg >> 11) * (15 - a) + (fg >> 11) * a) / 15;
    int g = (((bg >> 5) & 63) * (15 - a) + ((fg >> 5) & 63) * a) / 15;
    int b = ((bg & 31) * (15 - a) + (fg & 31) * a) / 15;
    return r << 11 | g << 5 | b;
}

/* run-length coded 4-bit bitmap (tools/mkassets.py): 00nnnnnn n+1 x bg, 01nnnnnn n+1 x fg,
 * 1vvvvnnn n+1 x level v; decoded straight into the window, no buffer. Whole bitmap on
 * screen only (no clipping): drawn nowhere otherwise. */
void lcd_blit_rle(int x, int y, int w, int h, const u8 *code, u16 fg, u16 bg) {
    u16 pal[16];
    u32 n = (u32)w * h;
    int i, x0 = x, y0 = y, cw = w, ch = h;
    if (x < 0 || y < 0 || x + w > lcd.width || y + h > lcd.height) return;
    if (!window(&x0, &y0, &cw, &ch)) return;
    for (i = 0; i < 16; i++) pal[i] = blend(fg, bg, i);
    while (n) {
        u8 b = *code++;
        u32 k = b < 0x80 ? (b & 0x3F) + 1 : (b & 7) + 1;
        u16 col = pal[b < 0x80 ? (b & 0x40 ? 15 : 0) : b >> 3 & 15];
        if (k > n) k = n;
        n -= k;
        while (k--) {
            spi_byte(col >> 8);
            spi_byte(col);
        }
    }
    gpio_write(LCD_CS, 1);
}
