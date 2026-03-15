//metronome
#include <driver/gpio.h>
#include <driver/timer.h>
#include <driver/periph_ctrl.h>

#define OUTPUT_PIN 25

void IRAM_ATTR timer_isr(void* arg)
{
   gpio_set_level(OUTPUT_PIN, !gpio_get_level(OUTPUT_PIN));
}

void app_main()
{
   timer_config_t config;
   config.divider = 80; // Set clock divider to 80
   config.counter_dir = TIMER_COUNT_UP; // Set counter direction to count up
   config.counter_en = TIMER_START; // Start the counter
   config.alarm_value.tv_usec = 500000; // Set alarm to 500ms
   config.intr_type = TIMER_INTR_LEVEL; // Set interrupt type to level
   config.auto_reload = 1; // Reload timer on alarm

   gpio_pad_select_gpio(OUTPUT_PIN);
   gpio_set_direction(OUTPUT_PIN, GPIO_MODE_OUTPUT);

   timer_init(TIMER_GROUP_0, TIMER_0, &config); // Initialize timer
   timer_enable_intr(TIMER_GROUP_0, TIMER_0); // Enable timer interrupt
   timer_isr_register(TIMER_GROUP_0, TIMER_0, timer_isr, NULL, 0, NULL); // Register timer interrupt
   timer_start(TIMER_GROUP_0, TIMER_0); // Start the timer
}
