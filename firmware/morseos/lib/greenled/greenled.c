/* MorseOS green LED, see greenled.h. */
#include "greenled.h"
#include "st7735.h"
#include "drivers/5316/clock.h"
#include "drivers/5316/gpio.h"

#define PIN GPIO_PC7

static int driven, lit;
static u32 t_keep;

int greenled_available(void) { return lcd_board && lcd_board->band == 2; }

void greenled_set(int on) {
    if (!greenled_available()) return;
    t_keep = clock_time();
    if (!driven) {
        gpio_set_func(PIN, AS_GPIO);
        gpio_setup_up_down_resistor(PIN, PM_PIN_UP_DOWN_FLOAT);
        gpio_write(PIN, 1);
        gpio_set_input_en(PIN, 0);
        gpio_set_output_en(PIN, 1);
        driven = 1;
    }
    if (on != lit) gpio_write(PIN, on ? 0 : 1);   /* active low */
    lit = on;
}

void greenled_poll(void) {
    if (!driven || clock_time() - t_keep < 300u * 1000 * sys_tick_per_us) return;
    gpio_write(PIN, 1);
    gpio_set_output_en(PIN, 0);                     /* released, as at boot */
    gpio_set_input_en(PIN, 1);
    driven = lit = 0;
}
