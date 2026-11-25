// Buttons: active low with pull-ups, polled every 20 ms; each new press goes into a queue.
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "badge/badge.h"
#include "badge/os.h"

static const char *TAG = "os";
static QueueHandle_t btnq;
static volatile int held;
static portMUX_TYPE home_lock = portMUX_INITIALIZER_UNLOCKED;
static bool home_prompt, home_requested;

void btn_home_prompt(bool enabled)
{
    portENTER_CRITICAL(&home_lock);
    home_prompt = enabled;
    home_requested = false;
    portEXIT_CRITICAL(&home_lock);
}

bool btn_home_requested(void)
{
    portENTER_CRITICAL(&home_lock);
    bool requested = home_requested;
    home_requested = false;
    portEXIT_CRITICAL(&home_lock);
    return requested;
}

bool btn_home_prompt_end(void)
{
    portENTER_CRITICAL(&home_lock);
    bool requested = home_requested;
    home_prompt = false;
    home_requested = false;
    portEXIT_CRITICAL(&home_lock);
    return requested;
}

static bool prompt_home(void)
{
    portENTER_CRITICAL(&home_lock);
    bool prompt = home_prompt;
    if (prompt) home_requested = true; // persistent even when the normal button queue is full
    portEXIT_CRITICAL(&home_lock);
    return prompt;
}

static void buttons(void *arg)
{
    static const int pin[4] = {40, 15, 39, 16}; // BTN_PLUS, BTN_MINUS, BTN_A, BTN_B (GPIOs of the printed names)
    gpio_config(&(gpio_config_t){.pin_bit_mask = BIT64(40) | BIT64(15) | BIT64(39) | BIT64(16), .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE});
    for (int was = 0, both = 0;; ms(20)) {
        int now = 0;
        for (int k = 0; k < 4; k++) now |= (!gpio_get_level(pin[k])) << k;
        held = now; // publish before a queued edge can wake a receiver on the other core
        for (int k = 0; k < 4; k++)
            if (now & ~was & 1 << k) xQueueSend(btnq, &k, 0);
        was = now;
        both = (now & 3) == 3 ? both + 20 : 0; // MENU + and MENU - together, held 0.4 s so a game cannot slip into it
        if (both == 400 && in_app) {
            if (prompt_home()) ESP_LOGI(TAG, "MENU + and MENU - held: app exit requested");
            else {
                ESP_LOGI(TAG, "MENU + and MENU - held: going home");
                esp_restart();
            }
        }
    }
}

void buttons_init(void)
{
    btnq = xQueueCreate(8, sizeof(int));
    xTaskCreate(buttons, "buttons", 3072, NULL, 3, NULL);
}

int btn_get(int timeout_ms)
{
    int k;
    return xQueueReceive(btnq, &k, timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms)) ? k : -1;
}

void btn_flush(void) { xQueueReset(btnq); }

int btn_held(void) { return held; }
