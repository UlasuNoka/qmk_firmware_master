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
extern bool is_fully_charged(void);
extern bool bt_is_disconnected_alert(void);

// 3 затверджені теми (20-20-20-20-20): [0]=1-20%, [1]=21-40%, [2]=41-60%, [3]=61-80%, [4]=81-100%
const uint8_t bat_themes_rgb[3][5][3] = {
    // 0. Classic: #FF0000 -> #FF8000 -> #FFFF00 -> #80C000 -> #008000
    {{0xFF, 0x00, 0x00}, {0xFF, 0x80, 0x00}, {0xFF, 0xFF, 0x00}, {0x80, 0xC0, 0x00}, {0x00, 0x80, 0x00}},
    // 1. Cyberpunk: #FF1744 -> #FF00AA -> #B000FF -> #5500FF -> #00E5FF
    {{0xFF, 0x17, 0x44}, {0xFF, 0x00, 0xAA}, {0xB0, 0x00, 0xFF}, {0x55, 0x00, 0xFF}, {0x00, 0xE5, 0xFF}},
    // 2. Royal Gold: #4B0082 -> #8000FF -> #C000FF -> #FF00AA -> #FFD700
    {{0x4B, 0x00, 0x82}, {0x80, 0x00, 0xFF}, {0xC0, 0x00, 0xFF}, {0xFF, 0x00, 0xAA}, {0xFF, 0xD7, 0x00}},
};

static uint8_t  aux_target_rgb[7][3];
static uint8_t  aux_cur_rgb[7][3];
static uint8_t  aux_fade_div[7] = {8, 8, 8, 8, 8, 8, 8};
static uint32_t aux_smooth_timer = 0;

static void aux_set_target(uint8_t idx, uint8_t r, uint8_t g, uint8_t b, uint8_t fade_div) {
    if (idx >= 7) return;
    aux_target_rgb[idx][0] = r;
    aux_target_rgb[idx][1] = g;
    aux_target_rgb[idx][2] = b;
    aux_fade_div[idx]      = (fade_div == 0) ? 1 : fade_div;
}

static void aux_flush_smooth(void) {
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
    for (uint8_t i = 0; i < BLED_LED_NUM; i++) {
        rgb_matrix_set_color(bled_leds[i], aux_cur_rgb[i][0], aux_cur_rgb[i][1], aux_cur_rgb[i][2]);
    }
    for (uint8_t i = 0; i < SLED_LED_NUM; i++) {
        rgb_matrix_set_color(sled_leds[i], aux_cur_rgb[2 + i][0], aux_cur_rgb[2 + i][1], aux_cur_rgb[2 + i][2]);
    }
}

void bled_task(void) {
    if (bt_is_disconnected_alert()) {
        bool phase = ((timer_read32() / 200) & 1) != 0;
        aux_set_target(0, phase ? 220 : 0, 0, 0, 2);
        aux_set_target(1, phase ? 0 : 220, 0, 0, 2);
        return;
    }
    if ((dev_info.devs != DEVS_USB) && !is_charging() && bts_info.bt_info.low_vol) {
        aux_set_target(0, 0, 0, 0, 12);
        aux_set_target(1, 0, 0, 0, 12);
        return;
    }
    uint8_t c_idx = (dev_info.bled_color > 0 && dev_info.bled_color < COLOR_COUNT) ? (dev_info.bled_color - 1) : 0;
    switch (dev_info.bled_mode) {
        case BLED_MODE_FLOW: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 4, 1));
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) {
                HSV hsv = {g_led_config.point[i].x - time, 255, bled_info.bled_val};
                RGB rgb = hsv_to_rgb(hsv);
                aux_set_target(i, rgb.r, rgb.g, rgb.b, 6);
            }
            break;
        }
        case BLED_MODE_NEON: {
            uint8_t time = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 8, 1));
            RGB     rgb  = hsv_to_rgb((HSV){time, 255, bled_info.bled_val});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) aux_set_target(i, rgb.r, rgb.g, rgb.b, 8);
            break;
        }
        case BLED_MODE_SOLID: {
            RGB rgb = hsv_to_rgb((HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], bled_info.bled_val});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) aux_set_target(i, rgb.r, rgb.g, rgb.b, 8);
            break;
        }
        case BLED_MODE_BREATHING: {
            uint8_t time       = scale16by8(g_rgb_timer, qadd8(bled_info.bled_speed / 4, 1));
            uint8_t brightness = scale8(abs8(sin8(time / 2) - 128) * 2, bled_info.bled_val);
            RGB     rgb        = hsv_to_rgb((HSV){hsv_table[c_idx][0], hsv_table[c_idx][1], brightness});
            for (uint8_t i = 0; i < BLED_LED_NUM; i++) aux_set_target(i, rgb.r, rgb.g, rgb.b, 6);
            break;
        }
        default: break;
    }
}

void sled_task(void) {
    bool chrg_full = is_charging() && is_fully_charged();

    uint8_t pvol = bts_info.bt_info.pvol;
    if (chrg_full || pvol == 0 || pvol > 100) {
        pvol = 100;
    }
#ifdef SLED_BAT_DEBUG
    if (sled_debug_pvol != 0) {
        pvol = sled_debug_pvol;
    }
#endif

    // Єдине округлення в БІЛЬШУ сторону (Ceiling): keys10 = 1..10, stop = 0..4 (рівні по 20%)
    uint8_t keys10 = (pvol + 9) / 10;
    if (keys10 == 0) keys10 = 1;
    if (keys10 > 10) keys10 = 10;
    uint8_t stop = (keys10 - 1) / 2;
    uint8_t pal  = dev_info.sled_palette % 3;

    // 1. Критичний розряд (<= 10% або low_vol без зарядки): пульсує лише 1-й крайній піксель до 50% яскравості
    if (!is_charging() && (bts_info.bt_info.low_vol || pvol <= 10)) {
        uint8_t max_v   = (bled_info.sled_val > 20) ? (bled_info.sled_val / 2) : 60;
        uint8_t pulse_v = scale8(255 - cos8(scale16by8(g_rgb_timer, 96)), max_v);
        uint8_t r = scale8(bat_themes_rgb[pal][0][0], pulse_v);
        uint8_t g = scale8(bat_themes_rgb[pal][0][1], pulse_v);
        uint8_t b = scale8(bat_themes_rgb[pal][0][2], pulse_v);
        for (uint8_t pos = 0; pos < SLED_LED_NUM; pos++) {
            uint8_t phys = dev_info.sled_dir ? (4 - pos) : pos;
            if (pos == 0) aux_set_target(2 + phys, r, g, b, 1);
            else          aux_set_target(2 + phys, 0, 0, 0, 1);
        }
        aux_flush_smooth();
        return;
    }

    uint8_t base_r = scale8(bat_themes_rgb[pal][stop][0], bled_info.sled_val);
    uint8_t base_g = scale8(bat_themes_rgb[pal][stop][1], bled_info.sled_val);
    uint8_t base_b = scale8(bat_themes_rgb[pal][stop][2], bled_info.sled_val);
    uint8_t mode   = dev_info.sled_mode % SLED_MODE_COUNT;

    if (mode == SLED_MODE_DOT) {
        // Режим 1 («Точка Заряду»): горить лише 1 піксель за позицією stop (0..4) та кольором stop (0..4)
        for (uint8_t pos = 0; pos < SLED_LED_NUM; pos++) {
            uint8_t phys = dev_info.sled_dir ? (4 - pos) : pos;
            if (pos == stop) {
                aux_set_target(2 + phys, base_r, base_g, base_b, 6);
            } else {
                aux_set_target(2 + phys, 0, 0, 0, 6);
            }
        }
    } else {
        // Режими 2 і 3 («Чортик із табакерки» — в один бік або маятником, без хвоста)
        uint32_t t_val      = (g_rgb_timer * (uint32_t)qadd8(bled_info.sled_speed / 2, 28)) >> 7;
        uint8_t  sub_phase  = (uint8_t)(t_val & 0xFF);
        uint32_t step_num   = t_val >> 8;
        uint8_t  env        = 255 - cos8(sub_phase); // 0 -> 255 -> 0 строго всередині фази одного пікселя!
        uint8_t  active_pos = 0;
        if (mode == SLED_MODE_JACK_ONEWAY) {
            active_pos = (uint8_t)(step_num % 5);
        } else {
            uint8_t p8 = (uint8_t)(step_num % 8);
            active_pos = (p8 < 5) ? p8 : (uint8_t)(8 - p8);
        }
        uint8_t r = scale8(base_r, env);
        uint8_t g = scale8(base_g, env);
        uint8_t b = scale8(base_b, env);
        for (uint8_t pos = 0; pos < SLED_LED_NUM; pos++) {
            uint8_t phys = dev_info.sled_dir ? (4 - pos) : pos;
            if (pos == active_pos) {
                aux_set_target(2 + phys, r, g, b, 1);
            } else {
                aux_set_target(2 + phys, 0, 0, 0, 1);
            }
        }
    }

    aux_flush_smooth();
}

void bled_low_indicate(void) {}
void bled_charging_indicate(void) {}
void bled_charged_indicate(void) {}
void bled_vol_indicate(void) {}
void bled_init(void) {
    dev_info.raw  = eeconfig_read_user();
    bled_info.raw = eeconfig_read_kb();
}

void bled_eeconfig_init(void) {
    dev_info.sled_mode    = SLED_MODE_DOT;
    dev_info.sled_dir     = 0;
    dev_info.sled_palette = 0;
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
