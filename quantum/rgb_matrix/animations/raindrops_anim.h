#ifdef ENABLE_RGB_MATRIX_RAINDROPS
RGB_MATRIX_EFFECT(RAINDROPS)
#    ifdef RGB_MATRIX_CUSTOM_EFFECT_IMPLS

static void raindrops_set_color(uint8_t i, effect_params_t* params) {
    if (!HAS_ANY_FLAGS(g_led_config.flags[i], params->flags)) return;
    hsv_t hsv = rgb_matrix_config.hsv;

    // Take the shortest path between hues
    int8_t deltaH = (int8_t)((hsv.h + 128) - hsv.h) / 4;
    hsv.h += (deltaH * random8_max(3));

    rgb_t rgb = rgb_matrix_hsv_to_rgb(hsv);
    rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
}

bool RAINDROPS(effect_params_t* params) {
    static uint16_t index   = RGB_MATRIX_LED_COUNT + 1;
    static uint8_t  rd_x    = 0;
    static uint8_t  rd_c    = 1;
    static uint16_t rd_left  = 0;
    static uint32_t rd_timer = 0;

    // Periodic trigger for LED change
    uint16_t step_ms = 640U - (((uint16_t)rgb_matrix_config.speed * 600U) / 255U);
    if ((params->iter == 0) && (timer_elapsed32(rd_timer) >= step_ms)) {
        rd_timer = timer_read32();
        const uint8_t mask = (RGB_MATRIX_LED_COUNT <= 128) ? 127U : 255U;
        do {
            if (rd_left == 0) {
                rd_left = (uint16_t)mask + 1U;
                rd_c    = (uint8_t)((timer_read32() | 1U) & mask);
            }
            rd_x = (uint8_t)((rd_x * 37U + rd_c) & mask);
            rd_left--;
        } while (rd_x >= RGB_MATRIX_LED_COUNT);
        index = rd_x;
    }

    RGB_MATRIX_USE_LIMITS(led_min, led_max);
    if (params->init) {
        for (uint8_t i = led_min; i < led_max; i++) {
            raindrops_set_color(i, params);
        }
    }
    // Change LED once and set index out of range till next trigger
    else if (led_min <= index && index < led_max) {
        raindrops_set_color(index, params);
        index = RGB_MATRIX_LED_COUNT + 1;
    }
    return rgb_matrix_check_finished_leds(led_max);
}

#    endif // RGB_MATRIX_CUSTOM_EFFECT_IMPLS
#endif     // ENABLE_RGB_MATRIX_RAINDROPS
