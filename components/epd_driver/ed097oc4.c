#include "ed097oc4.h"
#include "esp_timer.h"
#include "i2s_data_bus.h"
#include "rmt_pulse.h"

#include "xtensa/core-macros.h"

#if defined(CONFIG_EPD_BOARD_REVISION_V2)
#include "config_reg_v2.h"
#else
#if defined(CONFIG_EPD_BOARD_REVISION_V3)
#include "config_reg_v3.h"
#else
#error "unknown revision"
#endif
#endif

static epd_config_register_t config_reg;

/*
 * Write bits directly using the registers.
 * Won't work for some pins (>= 32).
 */
inline static void fast_gpio_set_hi(gpio_num_t gpio_num) {
  GPIO.out_w1ts = (1 << gpio_num);
}

inline static void fast_gpio_set_lo(gpio_num_t gpio_num) {
  GPIO.out_w1tc = (1 << gpio_num);
}

void IRAM_ATTR busy_delay(uint32_t cycles) {
  volatile unsigned long counts = XTHAL_GET_CCOUNT() + cycles;
  while (XTHAL_GET_CCOUNT() < counts) {
  };
}

inline static void IRAM_ATTR push_cfg_bit(bool bit) {
  fast_gpio_set_lo(CFG_CLK);
  if (bit) {
    fast_gpio_set_hi(CFG_DATA);
  } else {
    fast_gpio_set_lo(CFG_DATA);
  }
  fast_gpio_set_hi(CFG_CLK);
}

void epd_base_init(uint32_t epd_row_width) {

  config_reg_init(&config_reg);

  /* Power Control Output/Off */
  gpio_set_direction(CFG_DATA, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_CLK, GPIO_MODE_OUTPUT);
  gpio_set_direction(CFG_STR, GPIO_MODE_OUTPUT);

#if defined(CONFIG_EPD_BOARD_REVISION_V3)
  // use latch pin as GPIO
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[V3_LATCH_ENABLE], PIN_FUNC_GPIO);
  ESP_ERROR_CHECK(gpio_set_direction(V3_LATCH_ENABLE, GPIO_MODE_OUTPUT));
  gpio_set_level(V3_LATCH_ENABLE, 0);
#endif
  fast_gpio_set_lo(CFG_STR);

  push_cfg(&config_reg);

  // Setup I2S
  i2s_bus_config i2s_config;
  // add an offset off dummy bytes to allow for enough timing headroom
  i2s_config.epd_row_width = epd_row_width + 32;
  i2s_config.clock = CKH;
  i2s_config.start_pulse = STH;
  i2s_config.data_0 = D0;
  i2s_config.data_1 = D1;
  i2s_config.data_2 = D2;
  i2s_config.data_3 = D3;
  i2s_config.data_4 = D4;
  i2s_config.data_5 = D5;
  i2s_config.data_6 = D6;
  i2s_config.data_7 = D7;

  i2s_bus_init(&i2s_config);

  rmt_pulse_init(CKV);
}

void epd_poweron() {
    cfg_poweron(&config_reg);
}

void epd_poweroff() {
    cfg_poweroff(&config_reg);
}

void epd_start_frame() {
  while (i2s_is_busy() || rmt_busy()) {
  };
  config_reg.ep_mode = true;
  push_cfg(&config_reg);

  pulse_ckv_us(1, 1, true);

  // This is very timing-sensitive!
  config_reg.ep_stv = false;
  push_cfg(&config_reg);
  busy_delay(240);
  pulse_ckv_us(10, 10, false);
  config_reg.ep_stv = true;
  push_cfg(&config_reg);
  pulse_ckv_us(0, 10, true);

  config_reg.ep_output_enable = true;
  push_cfg(&config_reg);

  pulse_ckv_us(1, 1, true);
}

static inline void latch_row() {
#if defined(CONFIG_EPD_BOARD_REVISION_V2)
  config_reg.ep_latch_enable = true;
  push_cfg(&config_reg);

  config_reg.ep_latch_enable = false;
  push_cfg(&config_reg);
#else
#if defined(CONFIG_EPD_BOARD_REVISION_V3)
  fast_gpio_set_hi(V3_LATCH_ENABLE);
  fast_gpio_set_lo(V3_LATCH_ENABLE);
#else
#error "unknown revision"
#endif
#endif
}

void IRAM_ATTR epd_skip() {
#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2) || defined(CONFIG_EPD_DISPLAY_TYPE_ED133UT2)
  pulse_ckv_ticks(2, 2, false);
#else
  // According to the spec, the OC4 maximum CKV frequency is 200kHz.
  pulse_ckv_ticks(45, 5, false);
#endif
}

void IRAM_ATTR epd_output_row(uint32_t output_time_dus) {

  while (i2s_is_busy() || rmt_busy()) {
  };

  latch_row();

#if defined(CONFIG_EPD_DISPLAY_TYPE_ED097TC2) || defined(CONFIG_EPD_DISPLAY_TYPE_ED133UT2)
  pulse_ckv_ticks(output_time_dus, 1, false);
#else
  pulse_ckv_ticks(output_time_dus, 50, false);
#endif

  i2s_start_line_output();
  i2s_switch_buffer();
}

void epd_end_frame() {
  config_reg.ep_output_enable = false;
  push_cfg(&config_reg);
  config_reg.ep_mode = false;
  push_cfg(&config_reg);
  pulse_ckv_us(1, 1, true);
  pulse_ckv_us(1, 1, true);
}

void IRAM_ATTR epd_switch_buffer() { i2s_switch_buffer(); }
uint8_t IRAM_ATTR *epd_get_current_buffer() {
  return (uint8_t *)i2s_get_current_buffer();
};
