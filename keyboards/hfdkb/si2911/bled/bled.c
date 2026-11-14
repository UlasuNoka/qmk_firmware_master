/* Copyright (C) 2022 jonylee@hfd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "bled.h"
#include "../common/bt_task.h"
#include "lib/lib8tion/lib8tion.h"

#define BLED_VAL_STEP 64

bled_info_t bled_info = {0};

// Color table for fixed color modes
// clang-format off
const uint8_t hsv_table[COLOR_COUNT][3] = {
    {HSV_RED},
    {HSV_ORANGE},
    {HSV_YELLOW},
    {HSV_GREEN},
    {HSV_CYAN},
    {HSV_BLUE},
    {HSV_PURPLE},
    {HSV_WHITE},
};
// clang-format on

#define BLED_CYCLE_GROUP_SIZE 1
#define BLED_CYCLE_LED_COUNT 2
// Marquee effect with configurable group size
#define SLED_CYCLE_GROUP_SIZE 1 // Number of LEDs in each group
#define SLED_CYCLE_LED_COUNT 5  // Total LEDs (115 - 83 = 32)

static uint8_t bled_leds[] = {100, 101};
#define BLED_LED_NUM (sizeof(bled_leds) / sizeof(bled_leds[0]))
static uint8_t sled_leds[] = {102, 103, 104, 105, 106};
#define SLED_LED_NUM (sizeof(sled_leds) / sizeof(sled_leds[0]))

extern bool is_charging(void);
extern uint8_t bt_is_disconnected_alert(void);

static uint8_t  aux_target_rgb[7][3];
static uint8_t  aux_cur_rgb[7][3];
static uint8_t  aux_fade_div[7]  = {8, 8, 8, 8, 8, 8, 8};
static uint32_t aux_smooth_timer = 0;

void bled_set_aux_target(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t fade_div) {
    if (idx >= 7) return;
    aux_target_rgb[idx][0] = r;
    aux_target_rgb[idx][1] = g;
    aux_target_rgb[idx][2] = b;
    aux_fade_div[idx]      = (fade_div == 0) ? 1 : fade_div;
}

void bled_fade_all_off(void) {
    for (uint8_t i = 0; i < 7; i++) bled_set_aux_target(i, 0, 0, 0, 10);
}

void bled_flush_smooth(void) {
    if (timer_elapsed32(aux_smooth_timer) >= 8) {
        aux_smooth_timer = timer_read32();
        for (uint8_t i = 0; i < 7; i++) {
            uint8_t div = aux_fade_div[i];
            for (uint8_t c = 0; c < 3; c++) {
                int16_t diff = (int16_t)aux_target_rgb[i][c] - (int16_t)aux_cur_rgb[i][c];
                if (diff != 0) {
                    int16_t step = diff / (int16_t)div;
                    if (step == 0) step = (diff > 0) ? 1 : -1;
                    aux_cur_rgb[i][c] = (uint8_t)((int16_t)aux_cur_rgb[i][c] + step);
                }
            }
        }
    }
    for (uint8_t i = 0; i < BLED_LED_NUM; i++) rgb_matrix_set_color(bled_leds[i], aux_cur_rgb[i][0], aux_cur_rgb[i][1], aux_cur_rgb[i][2]);
    for (uint8_t i = 0; i < SLED_LED_NUM; i++) rgb_matrix_set_color(sled_leds[i], aux_cur_rgb[2 + i][0], aux_cur_rgb[2 + i][1], aux_cur_rgb[2 + i][2]);
}

void bled_task(void) {
    uint8_t alert_mode = bt_is_disconnected_alert();
    if (alert_mode != 0) {
        uint8_t w = sin8((uint8_t)(timer_read32() >> ((alert_mode == 2) ? 1 : 2)));
        bled_set_aux_target(0, scale8(220, scale8(w, w)), 0, 0, 1);
        bled_set_aux_target(1, scale8(220, scale8(255 - w, 255 - w)), 0, 0, 1);
        return;
    }
    if ((dev_info.devs != DEVS_USB) && !is_charging() && bts_info.bt_info.low_vol) {
        bled_set_aux_target(0, 0, 0, 0, 12);
        bled_set_aux_target(1, 0, 0, 0, 12);
        return;
    }
    uint8_t c_idx = (dev_info.bled_color > 0 && dev_info.bled_color < COLOR_COUNT) ? (dev_info.bled_color - 1) : 0;
    switch (dev_info.bled_mode) {
        case BLED_MODE_FLOW: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 4, 1));
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) {
                RGB rgb = hsv_to_rgb((HSV){g_led_config.point[bled_leds[i]].x - time, 255, bled_info.bled_val});
                bled_set_aux_target(i, rgb.r, rgb.g, rgb.b, 6);
            }
            break;
        }
        case BLED_MODE_NEON: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 8, 1));
            RGB     rgb  = hsv_to_rgb((HSV){time, 255, bled_info.bled_val});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) bled_set_aux_target(i, rgb.r, rgb.g, rgb.b, 8);
            break;
        }
        case BLED_MODE_SOLID: {
            RGB rgb = hsv_to_rgb((HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], bled_info.bled_val});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) bled_set_aux_target(i, rgb.r, rgb.g, rgb.b, 8);
            break;
        }
        case BLED_MODE_BREATHING: {
            uint8_t time       = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 4, 1));
            uint8_t brightness = scale8(abs8(sin8(time / 2) - 128) * 2, bled_info.bled_val);
            RGB     rgb        = hsv_to_rgb((HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], brightness});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) bled_set_aux_target(i, rgb.r, rgb.g, rgb.b, 6);
            break;
        }
        default: break;
    }
}

void sled_task(void) {
    uint8_t c_idx = (dev_info.sled_color > 0 && dev_info.sled_color < COLOR_COUNT) ? (dev_info.sled_color - 1) : 0;
    switch (dev_info.sled_mode) {
        case SLED_MODE_FLOW: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.sled_speed / 4, 1));
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
                RGB rgb = hsv_to_rgb((HSV){g_led_config.point[sled_leds[i]].x - time, 255, bled_info.sled_val});
                bled_set_aux_target(2 + i, rgb.r, rgb.g, rgb.b, 6);
            }
            break;
        }
        case SLED_MODE_CYCLE: {
            uint8_t time    = scale16by8(g_rgb_timer, qadd8(bled_info.sled_speed / 63, 1));
            uint8_t cyc_len = SLED_CYCLE_LED_COUNT * 2;
            uint8_t pos     = time % cyc_len;
            uint8_t led_pos = (pos < SLED_CYCLE_LED_COUNT) ? pos : (cyc_len - 1 - pos);
            HSV base_hsv    = (dev_info.sled_color == COLOR_RAINBOW)
                                  ? (HSV){time * 16, 255, bled_info.sled_val}
                                  : (HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], bled_info.sled_val};
            RGB rgb = hsv_to_rgb(base_hsv);
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
                if (i == led_pos) bled_set_aux_target(2 + i, rgb.r, rgb.g, rgb.b, 1);
                else bled_set_aux_target(2 + i, 0, 0, 0, 1);
            }
            break;
        }
        case SLED_MODE_NEON: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.sled_speed / 8, 1));
            RGB     rgb  = hsv_to_rgb((HSV){time, 255, bled_info.sled_val});
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) bled_set_aux_target(2 + i, rgb.r, rgb.g, rgb.b, 8);
            break;
        }
        case SLED_MODE_BREATHING: {
            uint8_t time       = scale16by8(g_rgb_timer, qadd8(bled_info.sled_speed / 4, 1));
            uint8_t brightness = scale8(abs8(sin8(time / 2) - 128) * 2, bled_info.sled_val);
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
                HSV hsv = (dev_info.sled_color == COLOR_RAINBOW) ? (HSV){i * 45, 255, brightness} : (HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], brightness};
                RGB rgb = hsv_to_rgb(hsv);
                bled_set_aux_target(2 + i, rgb.r, rgb.g, rgb.b, 6);
            }
            break;
        }
        case SLED_MODE_SOLID: {
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
                HSV hsv = (dev_info.sled_color == COLOR_RAINBOW) ? (HSV){i * 45, 255, bled_info.sled_val} : (HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], bled_info.sled_val};
                RGB rgb = hsv_to_rgb(hsv);
                bled_set_aux_target(2 + i, rgb.r, rgb.g, rgb.b, 8);
            }
            break;
        }
        case SLED_MODE_PULSE_STEP: {
            static uint32_t p_accum = 0;
            static uint32_t p_timer = 0;
            uint32_t        dt      = timer_elapsed32(p_timer);
            if (dt > 50) dt = 50;
            p_timer = timer_read32();
            uint32_t speed_mult = 30UL + ((uint32_t)bled_info.sled_speed / 4UL);
            p_accum += dt * speed_mult;
            uint8_t sub     = (uint8_t)((p_accum >> 8) & 0xFF);
            uint8_t stp     = (uint8_t)((p_accum >> 16) & 0x07);
            uint8_t led_pos = (stp < SLED_CYCLE_LED_COUNT) ? stp : (8U - stp);
            uint8_t w       = 0;
            if (sub < 90) {
                w = (uint8_t)(255U - cos8((uint8_t)(((uint16_t)sub * 128U) / 89U)));
            } else if (sub < 130) {
                w = 255;
            } else if (sub < 220) {
                w = cos8((uint8_t)(((uint16_t)(sub - 130U) * 128U) / 89U));
            }
            w = scale8(w, w);
            uint8_t step_hue = (uint8_t)(((p_accum >> 16) & 0xFF) * 32U);
            HSV     base_hsv = (dev_info.sled_color == COLOR_RAINBOW)
                                   ? (HSV){step_hue, 255, bled_info.sled_val}
                                   : (HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], bled_info.sled_val};
            RGB full_rgb = hsv_to_rgb(base_hsv);
            for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
                if (i == led_pos) bled_set_aux_target(2 + i, scale8(full_rgb.r, w), scale8(full_rgb.g, w), scale8(full_rgb.b, w), 2);
                else bled_set_aux_target(2 + i, 0, 0, 0, 2);
            }
            break;
        }
        default: break;
    }
}

void bled_low_indicate(void) {
    uint8_t max_v   = (bled_info.sled_val > 20) ? bled_info.sled_val : 120;
    uint8_t pulse_v = scale8(255 - cos8(scale16by8(g_rgb_timer, 96)), max_v);
    bled_set_aux_target(2, pulse_v, 0, 0, 2);
    for (uint8_t i = 1; i < SLED_LED_NUM; i++) bled_set_aux_target(2 + i, 0, 0, 0, 12);
}

void bled_charging_indicate(void) {
    uint8_t time      = scale16by8(g_rgb_timer / 4, qadd8(bled_info.sled_speed / 63, 1));
    uint8_t led_index = time % 5; // Cycle through 0-4

    // Clear all LEDs first
    for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
        rgb_matrix_set_color(sled_leds[i], RGB_OFF);
    }

    // Set the color
    // HSV base_hsv;
    // if (dev_info.sled_color == COLOR_RAINBOW) {
    //     base_hsv = (HSV){time * 16, 255, bled_info.sled_val};
    // } else {
    //     base_hsv.h = hsv_table[dev_info.sled_color - 1][0];
    //     base_hsv.s = hsv_table[dev_info.sled_color - 1][1];
    //     base_hsv.v = bled_info.sled_val;
    // }

    // Light up only the current LED in the cycle
    // RGB rgb = hsv_to_rgb(base_hsv);
    for (uint8_t i = 0; i <= led_index && i < SLED_LED_NUM; i++) {
        rgb_matrix_set_color(sled_leds[i], 0, 200, 0);
    }
}

void bled_charged_indicate(void) {
    // Show solid green to indicate fully charged
    for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
        rgb_matrix_set_color(sled_leds[i], RGB_GREEN);
    }
}

void bled_vol_indicate(void) {
    uint8_t query_index[] = {102, 103, 104, 105, 106};
    uint8_t pvol          = bts_info.bt_info.pvol;
    uint8_t led_count     = 0;
    RGB     color;

    if (pvol <= 20) {
        led_count = 1;
    } else if (pvol <= 40) {
        led_count = 2;
    } else if (pvol <= 60) {
        led_count = 3;
    } else if (pvol <= 80) {
        led_count = 4;
    } else {
        led_count = 5;
    }

    for (uint8_t i = 0; i < (sizeof(query_index) / sizeof(query_index[0])); i++) {
        rgb_matrix_set_color(query_index[i], RGB_OFF);
    }

    if (pvol <= 20) {
        color = (RGB){100, 0, 0}; // 红色
    } else {
        color = (RGB){0, 100, 0}; // 绿色
    }

    for (uint8_t i = 0; i < led_count; i++) {
        rgb_matrix_set_color(query_index[i], color.r, color.g, color.b);
    }
}

void bled_init(void) {
    dev_info.raw  = eeconfig_read_user();
    bled_info.raw = eeconfig_read_kb();
    if (bled_info.bled_val > RGB_MATRIX_MAXIMUM_BRIGHTNESS) bled_info.bled_val = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    if (bled_info.sled_val > RGB_MATRIX_MAXIMUM_BRIGHTNESS) bled_info.sled_val = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    if (dev_info.bled_mode >= BLED_MODE_COUNT) dev_info.bled_mode = BLED_MODE_FLOW;
    if (dev_info.sled_mode >= SLED_MODE_COUNT) dev_info.sled_mode = SLED_MODE_FLOW;
}

void bled_eeconfig_init(void) {
    dev_info.sled_mode  = SLED_MODE_FLOW;
    dev_info.bled_mode  = BLED_MODE_FLOW;
    dev_info.bled_color = COLOR_RED;
    dev_info.sled_color = COLOR_RED;
    eeconfig_update_user(dev_info.raw);

    bled_info.bled_val   = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    bled_info.bled_speed = RGB_MATRIX_DEFAULT_SPD;

    bled_info.sled_val   = RGB_MATRIX_MAXIMUM_BRIGHTNESS;
    bled_info.sled_speed = RGB_MATRIX_DEFAULT_SPD;
    eeconfig_update_kb(bled_info.raw);
}
