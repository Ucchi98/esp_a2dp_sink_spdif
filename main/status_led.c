#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"

#include "esp_log.h"

#include "status_led.h"

#ifdef CONFIG_STATUS_LED_RED_PIN
#define STATUS_LED_LEDC_GPIO_RED    (CONFIG_STATUS_LED_RED_PIN)
#else
#define STATUS_LED_LEDC_GPIO_RED    (17)
#endif

#ifdef CONFIG_STATUS_LED_BLUE_PIN
#define STATUS_LED_LEDC_GPIO_BLUE   (CONFIG_STATUS_LED_BLUE_PIN)
#else
#define STATUS_LED_LEDC_GPIO_BLUE   (18)
#endif

#define STATUS_LED_LEDC_CH_RED      (LEDC_CHANNEL_0)
#define STATUS_LED_LEDC_CH_BLUE     (LEDC_CHANNEL_1)
#define STATUS_LED_LEDC_DUTY_RES    (LEDC_TIMER_13_BIT)
#define STATUS_LED_LEDC_MAX_DUTY    (8191)
#define STATUS_LED_LEDC_GAMMA_STEPS (32)

static uint32_t status_led_ledc_gamma_table[STATUS_LED_LEDC_GAMMA_STEPS];

static TaskHandle_t h_status_led_fade_task = NULL;

void status_led_fade_up(ledc_channel_t ledc_channel);
void status_led_fade_down(ledc_channel_t ledc_channel);
void status_led_fade_start(ledc_fade_mode_t fade_mode);
void status_led_fade_up_blue();
void status_led_fade_up_blue_down_red();
void status_led_fade_up_red_down_blue();
void status_led_fade_loop_blue(int fade_dir);

void status_led_fade_task(void *pvParams);
void status_led_init_gamma_table(float gamma);

void status_led_init(void)
{
  // 1. タイマー設定
  ledc_timer_config_t ledc_timer = {
    .speed_mode       = LEDC_LOW_SPEED_MODE,
    .timer_num        = LEDC_TIMER_0,
    .duty_resolution  = STATUS_LED_LEDC_DUTY_RES,
    .freq_hz          = 5000,  // 5kHz
    .clk_cfg          = LEDC_AUTO_CLK
  };
  ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

  // 2. チャンネル設定
  ledc_channel_config_t ledc_channel_red = {
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = STATUS_LED_LEDC_CH_RED,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = STATUS_LED_LEDC_GPIO_RED,
    .duty           = 0, // 初期輝度は0
    .hpoint         = 0
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_red));

  ledc_channel_config_t ledc_channel_blue = {
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = STATUS_LED_LEDC_CH_BLUE,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = STATUS_LED_LEDC_GPIO_BLUE,
    .duty           = 0, // 初期輝度は0
    .hpoint         = 0
  };
  ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_blue));

  // 3. フェード機能の有効化（割り込みサービスの登録）
  ESP_ERROR_CHECK(ledc_fade_func_install(0));

  // 4. Make Gamma table
  status_led_init_gamma_table(2.2f);

  // 5. Start Fade Task
  xTaskCreate(
      status_led_fade_task,
      "status_led_fade_task",
      4096,
      NULL,
      5,
      &h_status_led_fade_task);
}

enum {
  STATUS_LED_STATE_FADE_UP_RED,
  STATUS_LED_STATE_FADE_UP_BLUE,
  STATUS_LED_STATE_FADE_UP_RED_DOWN_BLUE,
  STATUS_LED_STATE_FADE_UP_BLUE_DOWN_RED,
  STATUS_LED_STATE_FADE_LOOP_START,
  STATUS_LED_STATE_FADE_LOOP_STOP,
  STATUS_LED_STATE_FADE_STOP,
};

enum {
  STATUS_LED_FADE_DIR_DOWN,
  STATUS_LED_FADE_DIR_UP,
};

void status_led_set_status(int status)
{
  uint32_t st_fade = STATUS_LED_STATE_FADE_STOP;

  switch(status)
  {
    case STATUS_LED_BT_READY:
      st_fade = STATUS_LED_STATE_FADE_UP_RED;
      break;
    case STATUS_LED_BT_DISCONNECTED:
      st_fade = STATUS_LED_STATE_FADE_UP_RED_DOWN_BLUE;
      break;
    case STATUS_LED_BT_CONNECTED:
      st_fade = STATUS_LED_STATE_FADE_UP_BLUE_DOWN_RED;
      break;
    case STATUS_LED_AD_SUSPEND:
      st_fade = STATUS_LED_STATE_FADE_LOOP_STOP;
      break;
    case STATUS_LED_AD_STARTED:
      st_fade = STATUS_LED_STATE_FADE_LOOP_START;
      break;
  }
  xTaskNotify(h_status_led_fade_task, st_fade, eSetValueWithOverwrite);
}

void status_led_fade_up_blue_down_red()
{
  uint32_t duty_red;
  uint32_t duty_blue;
  for(int i=0 ; i<STATUS_LED_LEDC_GAMMA_STEPS ; i++)
  {
    duty_red  = status_led_ledc_gamma_table[STATUS_LED_LEDC_GAMMA_STEPS - 1 - i];
    duty_blue = status_led_ledc_gamma_table[i];

    // Fade to next_duty in 50ms
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_RED,  duty_red,  50);
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_BLUE, duty_blue, 50);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_RED,  LEDC_FADE_WAIT_DONE);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_BLUE, LEDC_FADE_WAIT_DONE);
  }
}

void status_led_fade_up_red_down_blue()
{
  uint32_t duty_red;
  uint32_t duty_blue;
  for(int i=0 ; i<STATUS_LED_LEDC_GAMMA_STEPS ; i++)
  {
    duty_red  = status_led_ledc_gamma_table[i];
    duty_blue = status_led_ledc_gamma_table[STATUS_LED_LEDC_GAMMA_STEPS - 1 - i];

    // Fade to next_duty in 50ms
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_RED,  duty_red,  50);
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_BLUE, duty_blue, 50);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_RED,  LEDC_FADE_WAIT_DONE);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, STATUS_LED_LEDC_CH_BLUE, LEDC_FADE_WAIT_DONE);
  }
}

void status_led_fade_loop(int channel, int fade_dir)
{
  uint32_t duty;
  for(int i=0 ; i<STATUS_LED_LEDC_GAMMA_STEPS ; i++)
  {
    if(fade_dir==STATUS_LED_FADE_DIR_UP)
      duty= status_led_ledc_gamma_table[i];
    else
      duty= status_led_ledc_gamma_table[STATUS_LED_LEDC_GAMMA_STEPS - 1 - i];

    // Fade to next_duty in 50ms
    ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, channel, duty, 50);
    ledc_fade_start(LEDC_LOW_SPEED_MODE, channel, LEDC_FADE_WAIT_DONE);
  }
}

void status_led_fade_task(void *pvParams)
{
  uint32_t st_fade = STATUS_LED_STATE_FADE_STOP;
  for(;;)
  {
    int st_fade_priv = st_fade;
    if(xTaskNotifyWait( 0x00, ULONG_MAX, &st_fade, pdMS_TO_TICKS(100))!=pdTRUE){
      st_fade = st_fade_priv;
    }

    switch(st_fade)
    {
      case STATUS_LED_STATE_FADE_UP_RED:
        status_led_fade_loop(STATUS_LED_LEDC_CH_RED, STATUS_LED_FADE_DIR_UP);
        st_fade = STATUS_LED_STATE_FADE_STOP;
        break;

      case STATUS_LED_STATE_FADE_UP_BLUE:
        status_led_fade_loop(STATUS_LED_LEDC_CH_BLUE, STATUS_LED_FADE_DIR_UP);
        st_fade = STATUS_LED_STATE_FADE_STOP;
        break;

      case STATUS_LED_STATE_FADE_UP_RED_DOWN_BLUE:
        status_led_fade_up_red_down_blue();
        st_fade = STATUS_LED_STATE_FADE_STOP;
        break;

      case STATUS_LED_STATE_FADE_UP_BLUE_DOWN_RED:
        status_led_fade_up_blue_down_red();
        st_fade = STATUS_LED_STATE_FADE_STOP;
        break;

      case STATUS_LED_STATE_FADE_LOOP_START:
        status_led_fade_loop(STATUS_LED_LEDC_CH_BLUE, STATUS_LED_FADE_DIR_DOWN);
        status_led_fade_loop(STATUS_LED_LEDC_CH_BLUE, STATUS_LED_FADE_DIR_UP);
        st_fade = STATUS_LED_STATE_FADE_LOOP_START;
        break;

      case STATUS_LED_STATE_FADE_LOOP_STOP:
        st_fade = STATUS_LED_STATE_FADE_STOP;
        break;

      case STATUS_LED_STATE_FADE_STOP:
        break;

      default:
        break;
    }
  }
  vTaskDelete(NULL); 
}

void status_led_init_gamma_table(float gamma)
{
  for(int i=0 ; i<STATUS_LED_LEDC_GAMMA_STEPS; i++)
  {
    status_led_ledc_gamma_table[i] = (uint32_t)(powf((float )i / (STATUS_LED_LEDC_GAMMA_STEPS- 1), gamma) * STATUS_LED_LEDC_MAX_DUTY);
  }
}
