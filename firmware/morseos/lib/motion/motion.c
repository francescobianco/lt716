/* MorseOS motion sensor (vibration switch on PA4), see motion.h. */
#include "motion.h"
#include "drivers/5316/gpio.h"

#define PIN GPIO_PA4

static int level = -1;
static u32 edges;

void motion_init(void) {     /* as the stock firmware: GPIO input, 10k pull-up */
    gpio_set_func(PIN, AS_GPIO);
    gpio_set_output_en(PIN, 0);
    gpio_set_input_en(PIN, 1);
    gpio_setup_up_down_resistor(PIN, PM_PIN_PULLUP_10K);
    level = gpio_read(PIN) ? 1 : 0;
}

void motion_poll(void) {
    int v;
    if (level < 0) return;
    v = gpio_read(PIN) ? 1 : 0;
    if (v != level) {
        level = v;
        edges++;
    }
}

int motion_level(void) { return level; }
u32 motion_edges(void) { return edges; }
void motion_clear(void) { edges = 0; }
