/* Reconstruction of lib_bts.a (bts_lib.o)
 * Verified against llvm-objdump, readelf, and bts_lib.h
 * Includes bounds-check fixes for modern GCC (-Werror=array-bounds / -Werror=stringop-overflow)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "bts_lib.h"
#include <string.h>

typedef struct {
    uint8_t *start;
    uint8_t *end;
} smsg_node_t;

typedef struct {
    uint8_t      state;         /* offset 0x00: 0=send, 1=wait_ack, 2=retry, 3=pop */
    smsg_node_t *base;          /* offset 0x04: &smsg_ptr[0] */
    smsg_node_t *write_ptr;     /* offset 0x08 */
    smsg_node_t *read_ptr;      /* offset 0x0C */
    uint8_t     *buf_write_ptr; /* offset 0x10 */
} smsg_ctrl_cb_t;

typedef struct {
    uint8_t  buf[32]; /* offset 0x00 */
    uint32_t len;     /* offset 0x20 */
} bt_send_info_t;

typedef struct {
    uint8_t buf[32]; /* offset 0x00 */
    uint8_t count;   /* offset 0x20 */
} bt_receive_info_t;

/* .data section */
uint16_t c_intervals[4] = {0x0010, 0x0020, 0x0010, 0x0010};
uint16_t c_offsets[4]   = {0x0010, 0x0001, 0x0004, 0x0020};
uint16_t w_intervals[4] = {0x0028, 0x0168, 0x0078, 0x0014};
uint16_t w_offsets[4]   = {0x0001, 0x0001, 0x0001, 0x0001};
static uint8_t nkro_enable = 1;

/* .rodata.CSWTCH.90 (27 entries: QMK 0xA8..0xC2 -> Consumer Usage) */
static const uint16_t CSWTCH_90[27] = {
    0x00E2, /* 0xA8: KC_MUTE */
    0x00E9, /* 0xA9: KC_VOLU */
    0x00EA, /* 0xAA: KC_VOLD */
    0x00B5, /* 0xAB: KC_MNXT */
    0x00B6, /* 0xAC: KC_MPRV */
    0x00B7, /* 0xAD: KC_MSTP */
    0x00CD, /* 0xAE: KC_MPLY */
    0x0183, /* 0xAF: KC_MSEL */
    0x00CC, /* 0xB0: KC_EJCT */
    0x018A, /* 0xB1: KC_MAIL */
    0x0192, /* 0xB2: KC_CALC */
    0x0194, /* 0xB3: KC_MYCM */
    0x0221, /* 0xB4: KC_WSCH */
    0x0223, /* 0xB5: KC_WHOM */
    0x0224, /* 0xB6: KC_WBAK */
    0x0225, /* 0xB7: KC_WFWD */
    0x0226, /* 0xB8: KC_WSTP */
    0x0227, /* 0xB9: KC_WREF */
    0x022A, /* 0xBA: KC_WFAV */
    0x00B3, /* 0xBB: KC_MFFD */
    0x00B4, /* 0xBC: KC_MRWD */
    0x006F, /* 0xBD: KC_BRIU */
    0x0070, /* 0xBE: KC_BRID */
    0x019F, /* 0xBF: KC_CPNL */
    0x01CB, /* 0xC0: KC_ASST */
    0x029F, /* 0xC1: MAC Mission Control */
    0x02A0  /* 0xC2: MAC Launchpad */
};

/* .rodata.CSWTCH.91 (5 entries: host index -> bt_name[] index) */
static const uint8_t CSWTCH_91[5] = {0, 1, 2, 3, 4};

/* .bss section */
static bt_receive_info_t bt_receive_info;
static bt_send_info_t    bt_send_info;
static bts_info_t       *bts_info;
static uint16_t          consumer_report_buf;
static uint8_t           f_report_update;
static uint8_t           flip_5725;
static uint8_t           kb_report_buf[8];
static uint8_t           mk_speed;
static int8_t            mouse_report_buf[5];
static uint8_t           nkro_report_buf[14];
static uint8_t           retrycnt_5737;
static uint8_t           smsg_buffer[800];
static smsg_ctrl_cb_t    smsg_ctrl_cb;
static uint8_t           smsg_get_data[20];
static smsg_node_t       smsg_ptr[40];
static uint8_t           smsg_set_data[20];
static uint32_t          smsg_timeout;
static uint8_t           system_report_buf;

/* Note: Original .rodata.bts_assert also verified WB32 silicon ID:
 * (*(volatile uint32_t *)0x1FFFF200 & 0xF0003FFF) == 0x30002980
 */
static void bts_assert(int cond) {
    if (!cond) {
        while (1) {
        }
    }
}

/* .text.smsg_pop */
static void smsg_pop(void) {
    smsg_node_t *w = smsg_ctrl_cb.write_ptr;
    smsg_node_t *r = smsg_ctrl_cb.read_ptr;
    if (r == w) {
        return;
    }
    smsg_node_t *base = smsg_ctrl_cb.base;
    if (r == base + 39) { /* base + 0x138 bytes */
        smsg_ctrl_cb.read_ptr = base;
    } else {
        smsg_ctrl_cb.read_ptr = r + 1;
    }
}

/* .text.adjust_speed.constprop.3 */
static void adjust_speed(void) {
    uint8_t idx   = mk_speed;
    int16_t c_off = (int16_t)c_offsets[idx];
    int16_t w_off = (int16_t)w_offsets[idx];

    if (mouse_report_buf[1] > 0) {
        mouse_report_buf[1] = (int8_t)c_off;
    }
    if (mouse_report_buf[1] < 0) {
        mouse_report_buf[1] = (int8_t)(-c_off);
    }

    if (mouse_report_buf[2] > 0) {
        mouse_report_buf[2] = (int8_t)c_off;
    }
    if (mouse_report_buf[2] < 0) {
        mouse_report_buf[2] = (int8_t)(-c_off);
    }

    if (mouse_report_buf[4] > 0) {
        mouse_report_buf[4] = (int8_t)w_off;
    }
    if (mouse_report_buf[4] < 0) {
        mouse_report_buf[4] = (int8_t)(-w_off);
    }

    if (mouse_report_buf[1] != 0 && mouse_report_buf[2] != 0) {
        int32_t x = (mouse_report_buf[1] * 0xB5) >> 8;
        if (x == 0) {
            x = 1;
        }
        int32_t y = (mouse_report_buf[2] * 0xB5) >> 8;
        if (y == 0) {
            y = 1;
        }
        mouse_report_buf[1] = (int8_t)x;
        mouse_report_buf[2] = (int8_t)y;
    }

    if (mouse_report_buf[4] != 0) {
        mouse_report_buf[4] = (int8_t)((mouse_report_buf[4] * 0xB5) >> 8);
    }
}

/* .text.cl_check_sum.constprop.7 */
static void cl_check_sum(uint32_t len) {
    uint8_t  sum = 0;
    uint8_t *p   = bt_send_info.buf;
    for (uint32_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    bt_send_info.buf[len] = sum;
}

/* .text.set_sevent */
static bool set_sevent(uint8_t cmd, const uint8_t *data, uint8_t len) {
    if (len > 19) {
        return false;
    }
    uint8_t total = (uint8_t)(len + 1);

    smsg_set_data[0] = cmd;
    if (len > 0 && data != NULL) {
        memcpy(&smsg_set_data[1], data, len);
    }

    smsg_node_t *base  = smsg_ctrl_cb.base;
    smsg_node_t *w     = smsg_ctrl_cb.write_ptr;
    smsg_node_t *end_w = base + 39; /* base + 0x138 bytes */
    smsg_node_t *r     = smsg_ctrl_cb.read_ptr;

    if (w == end_w) {
        if (base == r) {
            return false;
        }
    } else {
        if (r == w + 1) {
            return false;
        }
    }

    if ((uint32_t)((&smsg_buffer[799]) - smsg_ctrl_cb.buf_write_ptr) <= 19) {
        smsg_ctrl_cb.buf_write_ptr = smsg_buffer;
    }

    uint8_t *dst = smsg_ctrl_cb.buf_write_ptr;
    memcpy(dst, smsg_set_data, total);

    w->start                   = dst;
    w->end                     = dst + total;
    smsg_ctrl_cb.buf_write_ptr = dst + total;
    smsg_ctrl_cb.write_ptr     = (w == end_w) ? base : (w + 1);
    return true;
}

/* .text.bts_send_mouse_report.part.0 */
static bool bts_send_mouse_report_part0(uint8_t *report) {
    memcpy(mouse_report_buf, report, 5);
    return set_sevent(7, (const uint8_t *)mouse_report_buf, 5);
}

/* .text.bts_init */
void bts_init(bts_info_t *info) {
    bts_assert(info != NULL);
    bts_assert(info->uart_init != NULL);
    bts_assert(1);
    bts_assert(info->timer_read32 != NULL);

    smsg_ctrl_cb.base          = smsg_ptr;
    smsg_ctrl_cb.buf_write_ptr = smsg_buffer;
    smsg_ptr[0].start          = smsg_buffer;
    smsg_ptr[0].end            = smsg_buffer;
    smsg_ctrl_cb.write_ptr     = smsg_ptr;
    smsg_ctrl_cb.read_ptr      = smsg_ptr;
    smsg_ctrl_cb.state         = 0;

    bts_info = info;
    info->uart_init(115200); /* 0x1C200 */

    memset(&bts_info->bt_info, 0, sizeof(bt_info_t));
    bts_info->bt_info.normal_vol = true; /* offset 0x17 */
    bts_info->bt_info.pairing    = true; /* offset 0x18 */
    bts_info->bt_info.pvol       = 100;  /* offset 0x1D */

    nkro_enable     = 1;
    f_report_update = 0;
    memset(kb_report_buf, 0, 8);
    memset(nkro_report_buf, 0, 14);
    consumer_report_buf = 0;
    memset(mouse_report_buf, 0, 5);
    system_report_buf = 0;
}

/* .text.bts_get_version */
char *bts_get_version(void) {
    return (char *)"bts lib version: v1.1.1";
}

/* .text.bts_is_busy */
uint8_t bts_is_busy(void) {
    return (smsg_ctrl_cb.read_ptr != smsg_ctrl_cb.write_ptr) ? 1 : 0;
}

/* .text.bts_set_nkro */
void bts_set_nkro(bool state) {
    nkro_enable = state ? 1 : 0;
}

/* .text.bts_get_nkro */
bool bts_get_nkro(void) {
    return nkro_enable != 0;
}

/* .text.bts_test_report_rate_task */
void bts_test_report_rate_task(void) {
    switch (flip_5725) {
        case 0:
            mouse_report_buf[1] = 10;
            mouse_report_buf[2] = 0;
            if (bts_send_mouse_report_part0((uint8_t *)mouse_report_buf)) {
                flip_5725 = 1;
            }
            break;
        case 1:
            mouse_report_buf[1] = 0;
            mouse_report_buf[2] = -10;
            if (bts_send_mouse_report_part0((uint8_t *)mouse_report_buf)) {
                flip_5725 = 2;
            }
            break;
        case 2:
            mouse_report_buf[1] = -10;
            mouse_report_buf[2] = 0;
            if (bts_send_mouse_report_part0((uint8_t *)mouse_report_buf)) {
                flip_5725 = 3;
            }
            break;
        case 3:
            mouse_report_buf[1] = 0;
            mouse_report_buf[2] = 10;
            if (bts_send_mouse_report_part0((uint8_t *)mouse_report_buf)) {
                flip_5725 = 0;
            }
            break;
        default:
            break;
    }
}

/* .text.bts_send_vendor */
bool bts_send_vendor(vbs_t cmd) {
    uint8_t c = (uint8_t)cmd;
    return set_sevent(6, &c, 1);
}

/* .text.bts_send_fn */
bool bts_send_fn(bool pressed) {
    uint8_t val = pressed ? 0x10 : 0x00;
    return set_sevent(5, &val, 1);
}

/* .text.bts_send_name */
bool bts_send_name(devs_t host) {
    bts_assert(bts_info != NULL);
    uint8_t idx = (uint8_t)(host - 1);
    if (idx > 4) {
        return false;
    }
    const char *name = bts_info->bt_name[CSWTCH_91[idx]];
    if (!name) {
        return false;
    }
    return set_sevent(8, (const uint8_t *)name, (uint8_t)strlen(name));
}

/* .text.bts_send_mouse_report */
bool bts_send_mouse_report(uint8_t *report) {
    if (!report) {
        return false;
    }
    return bts_send_mouse_report_part0(report);
}

/* .text.bts_task */
void bts_task(devs_t dev_state) {
    bts_assert(bts_info != NULL);
    bts_assert(bts_info != NULL);
    bts_assert(bts_info->uart_available != NULL);
    bts_assert(bts_info->uart_read != NULL);

    /* 1. Process incoming 3-byte UART frames */
    while (bts_info->uart_available()) {
        uint8_t cnt = bt_receive_info.count;
        if (cnt <= 2) {
            bt_receive_info.count    = cnt + 1;
            bt_receive_info.buf[cnt] = bts_info->uart_read();
            if (bt_receive_info.count <= 2) {
                break;
            }
        } else {
            bt_receive_info.buf[0] = bt_receive_info.buf[1];
            bt_receive_info.buf[1] = bt_receive_info.buf[2];
            bt_receive_info.buf[2] = bts_info->uart_read();
        }

        uint8_t b0 = bt_receive_info.buf[0];
        uint8_t b1 = bt_receive_info.buf[1];
        uint8_t b2 = bt_receive_info.buf[2];

        /* Check for "a\r\n" ACK */
        if (b0 == 0x61 && b1 == 0x0D && b2 == 0x0A) {
            bt_receive_info.count     = 0;
            bts_info->bt_info.sleeped = false;
            smsg_ctrl_cb.state        = 3;
            continue;
        }
        /* Check for [0x5B, 0x43, 0x9E] ACK */
        if (b0 == 0x5B && b1 == 0x43) {
            if (b2 == 0x9E) {
                smsg_ctrl_cb.state    = 3;
                bt_receive_info.count = 0;
            }
            continue;
        }

        /* Verify 8-bit checksum */
        if ((uint8_t)(b0 + b1) != b2 || bt_receive_info.count != 3) {
            continue;
        }

        if (b1 != 0x42) {
            uint8_t dummy = 0;
            set_sevent(9, &dummy, 0); /* Enqueue "a\r\n" ACK */
        }

        bt_receive_info.count = 0;

        if (b0 == 0x5A) {
            if ((b1 & 0xF8) == 0) {
                bts_info->bt_info.indictor_rgb_s = b1;
            }
        } else if (b0 == 0x5C) {
            bts_info->bt_info.pvol = b1;
        } else if (b0 == 0x5B) {
            switch (b1) {
                case 0x21:
                    bts_info->bt_info.low_vol = true;
                    bts_info->bt_info.pvol    = 10;
                    break;
                case 0x22:
                    bts_info->bt_info.low_vol_offed = true;
                    bts_info->bt_info.normal_vol    = false;
                    bts_info->bt_info.pvol          = 1;
                    break;
                case 0x23:
                    bts_info->bt_info.low_vol       = false;
                    bts_info->bt_info.low_vol_offed = false;
                    bts_info->bt_info.normal_vol    = true;
                    bts_send_vendor(v_query_vol);
                    break;
                case 0x31:
                    bts_info->bt_info.pairing = true;
                    break;
                case 0x32:
                    bts_info->bt_info.pairing = false;
                    bts_info->bt_info.paired  = true;
                    break;
                case 0x33:
                    bts_info->bt_info.pairing = false;
                    bts_info->bt_info.paired  = false;
                    break;
                case 0x34:
                    bts_info->bt_info.mode_switched = true;
                    break;
                case 0x35:
                    bts_info->bt_info.come_back = true;
                    break;
                case 0x36:
                    bts_info->bt_info.come_back_err = true;
                    bts_send_vendor(v_pair);
                    break;
                case 0x42:
                    bts_info->bt_info.sleeped = false;
                    smsg_ctrl_cb.state        = 2;
                    break;
                default:
                    break;
            }
        }
    }

    /* 2. Process TX state machine */
    bts_assert(bts_info != NULL);

    if (smsg_ctrl_cb.state == 3) {
        smsg_pop();
        smsg_ctrl_cb.state = 0;
        retrycnt_5737      = 0;
    }

    if (smsg_ctrl_cb.state == 0) {
        if (smsg_ctrl_cb.read_ptr != smsg_ctrl_cb.write_ptr) {
            uint8_t *start = smsg_ctrl_cb.read_ptr->start;
            uint8_t *end   = smsg_ctrl_cb.read_ptr->end;
            uint32_t len   = (uint32_t)(end - start);
            memcpy(smsg_get_data, start, len);

            if (len != 0) {
                bts_assert(bts_info != NULL);
                uint8_t cmd = smsg_get_data[0];
                if (cmd <= 9) {
                    bool send_with_ack = true;
                    switch (cmd) {
                        case 0:
                            bt_send_info.len    = 3;
                            bt_send_info.buf[0] = 0xA6;
                            bt_send_info.buf[1] = 0x00;
                            cl_check_sum(2);
                            break;
                        case 1:
                            if (bts_info->bt_info.sleeped) {
                                uint8_t dummy = 0;
                                set_sevent(0, &dummy, 0);
                                smsg_ctrl_cb.state = 3;
                                send_with_ack      = false;
                            } else {
                                bt_send_info.len    = 10;
                                bt_send_info.buf[0] = 0xA1;
                                memcpy(&bt_send_info.buf[1], &smsg_get_data[1], 8);
                                cl_check_sum(9);
                            }
                            break;
                        case 2:
                            bt_send_info.len    = 16;
                            bt_send_info.buf[0] = 0xA2;
                            memcpy(&bt_send_info.buf[1], &smsg_get_data[1], 14);
                            cl_check_sum(15);
                            break;
                        case 3:
                            bt_send_info.len    = 4;
                            bt_send_info.buf[0] = 0xA3;
                            bt_send_info.buf[1] = smsg_get_data[1];
                            bt_send_info.buf[2] = smsg_get_data[2];
                            cl_check_sum(3);
                            break;
                        case 4:
                            bt_send_info.len    = 3;
                            bt_send_info.buf[0] = 0xA4;
                            bt_send_info.buf[1] = smsg_get_data[1];
                            cl_check_sum(2);
                            break;
                        case 5:
                            bt_send_info.len    = 3;
                            bt_send_info.buf[0] = 0xA5;
                            bt_send_info.buf[1] = smsg_get_data[1];
                            cl_check_sum(2);
                            break;
                        case 6:
                            bt_send_info.len    = 3;
                            bt_send_info.buf[0] = 0xA6;
                            bt_send_info.buf[1] = smsg_get_data[1];
                            cl_check_sum(2);
                            break;
                        case 7:
                            bt_send_info.len    = 7;
                            bt_send_info.buf[0] = 0xA8;
                            memcpy(&bt_send_info.buf[1], &smsg_get_data[1], 5);
                            cl_check_sum(6);
                            break;
                        case 8: {
                            uint8_t payload_len = (uint8_t)len;
                            bt_send_info.len    = payload_len + 3;
                            bt_send_info.buf[0] = 0xA9;
                            bt_send_info.buf[1] = payload_len;
                            memcpy(&bt_send_info.buf[2], &smsg_get_data[1], payload_len - 1);
                            bt_send_info.buf[1 + payload_len] = 0;
                            cl_check_sum(payload_len + 2);
                            break;
                        }
                        case 9:
                            bt_send_info.len    = 3;
                            bt_send_info.buf[0] = 0x61;
                            bt_send_info.buf[1] = 0x0D;
                            bt_send_info.buf[2] = 0x0A;
                            bts_assert(bts_info->uart_transmit != NULL);
                            bts_info->uart_transmit(bt_send_info.buf, (uint16_t)bt_send_info.len);
                            smsg_ctrl_cb.state = 3;
                            send_with_ack      = false;
                            break;
                    }
                    if (send_with_ack) {
                        bts_assert(bts_info->uart_transmit != NULL);
                        bts_info->uart_transmit(bt_send_info.buf, (uint16_t)bt_send_info.len);
                        smsg_timeout       = bts_info->timer_read32();
                        smsg_ctrl_cb.state = 1;
                    }
                } else {
                    smsg_ctrl_cb.state = 3;
                }
            }
        }
    } else if (smsg_ctrl_cb.state == 1) {
        uint32_t now     = bts_info->timer_read32();
        uint32_t elapsed = (now >= smsg_timeout) ? (bts_info->timer_read32() - smsg_timeout) : (bts_info->timer_read32() + ~smsg_timeout);
        if (elapsed > 5) {
            smsg_ctrl_cb.state = 2;
        }
    } else if (smsg_ctrl_cb.state == 2) {
        uint8_t r = (uint8_t)(retrycnt_5737 + 1);
        if (r <= 3) {
            retrycnt_5737      = r;
            smsg_ctrl_cb.state = 0;
        } else {
            retrycnt_5737 = 0;
            smsg_pop();
            smsg_ctrl_cb.state = 0;
        }
    }

    /* 3. Flush dirty keyboard reports */
    if (dev_state != DEVS_USB) {
        if (f_report_update & 0x01) {
            f_report_update &= ~0x01;
            set_sevent(1, kb_report_buf, 8);
        }
        if (f_report_update & 0x02) {
            f_report_update &= ~0x02;
            set_sevent(2, nkro_report_buf, 14);
        }
    }
}

/* .text.bts_process_keys */
bool bts_process_keys(uint16_t keycode, bool pressed, devs_t dev_state, bool no_gui, uint8_t num) {
    (void)dev_state;
    (void)num;
    bts_assert(bts_info != NULL);

    /* 1. Standard keys: 0x04 (KC_A) .. 0xA4 (KC_EXSEL) */
    if ((uint16_t)(keycode - 0x04) <= 0xA0) {
        uint8_t byte_idx = (uint8_t)(keycode >> 3);
        uint8_t bit_pos  = (uint8_t)(keycode & 0x07);

        if (pressed) {
            uint8_t slot = 2;
            for (; slot < 7; slot++) {
                uint8_t cur = kb_report_buf[slot];
                if (cur == (uint8_t)keycode) {
                    break;
                }
                if (cur == 0) {
                    kb_report_buf[slot] = (uint8_t)keycode;
                    f_report_update |= 0x01;
                    break;
                }
            }
            if (nkro_enable && slot == 7 && byte_idx < sizeof(nkro_report_buf)) {
                nkro_report_buf[byte_idx] |= (uint8_t)(1U << bit_pos);
                f_report_update |= 0x02;
            }
        } else {
            for (uint8_t slot = 2; slot < 8; slot++) {
                if (kb_report_buf[slot] == (uint8_t)keycode) {
                    kb_report_buf[slot] = 0;
                    f_report_update |= 0x01;
                    break;
                }
            }
            if (byte_idx < sizeof(nkro_report_buf) && ((nkro_report_buf[byte_idx] >> bit_pos) & 0x01)) {
                nkro_report_buf[byte_idx] &= (uint8_t)~(1U << bit_pos);
                f_report_update |= 0x02;
            }
        }
        return false;
    }

    /* 2. Modifiers: 0xE0 (KC_LCTL) .. 0xE7 (KC_RGUI) */
    if ((uint16_t)(keycode - 0xE0) <= 0x07) {
        uint8_t bit = (uint8_t)(keycode - 0xE0);
        if (pressed) {
            if (!(no_gui && keycode == 0xE3)) {
                kb_report_buf[0] |= (uint8_t)(1U << bit);
            }
        } else {
            kb_report_buf[0] &= (uint8_t)~(1U << bit);
        }
        f_report_update |= 0x01;
        return false;
    }

    /* 3. Mouse keys: 0xCD .. 0xDF */
    if ((uint16_t)(keycode - 0xCD) <= 0x12) {
        uint8_t k   = (uint8_t)keycode;
        uint8_t spd = mk_speed;
        if (pressed) {
            if (keycode == 0xCD) {
                mouse_report_buf[2] = (int8_t)(-c_offsets[spd]);
            } else if (keycode == 0xCE) {
                mouse_report_buf[2] = (int8_t)(c_offsets[spd]);
            } else if (k == 0xCF) {
                mouse_report_buf[1] = (int8_t)(-c_offsets[spd]);
            } else if (k == 0xD0) {
                mouse_report_buf[1] = (int8_t)(c_offsets[spd]);
            } else if (k == 0xDB) {
                mouse_report_buf[4] = (int8_t)(-w_offsets[spd]);
            } else if (k == 0xDC) {
                mouse_report_buf[4] = (int8_t)(w_offsets[spd]);
            } else if ((uint8_t)(k - 0xD1) <= 7) {
                mouse_report_buf[0] |= (int8_t)(1U << (k - 0xD1));
            } else {
                if (k == 0xDD) {
                    mk_speed = 1;
                } else if (k == 0xDE) {
                    mk_speed = 2;
                } else if (k == 0xDF) {
                    mk_speed = 3;
                }
                if (mk_speed != spd) {
                    adjust_speed();
                }
            }
        } else {
            if (keycode == 0xCD && mouse_report_buf[2] < 0) {
                mouse_report_buf[2] = 0;
            } else if (keycode == 0xCE && mouse_report_buf[2] > 0) {
                mouse_report_buf[2] = 0;
            } else if (keycode == 0xCF && mouse_report_buf[1] < 0) {
                mouse_report_buf[1] = 0;
            } else if (keycode == 0xD0 && mouse_report_buf[1] > 0) {
                mouse_report_buf[1] = 0;
            } else if (keycode == 0xDB && mouse_report_buf[4] < 0) {
                mouse_report_buf[4] = 0;
            } else if (keycode == 0xDC && mouse_report_buf[4] > 0) {
                mouse_report_buf[4] = 0;
            } else if ((uint8_t)(k - 0xD1) <= 7) {
                mouse_report_buf[0] &= (int8_t)~(1U << (k - 0xD1));
            } else if ((uint8_t)(k - 0xDD) <= 2) {
                mk_speed = 0;
                if (spd != 0) {
                    adjust_speed();
                }
            }
        }
        bts_send_mouse_report((uint8_t *)mouse_report_buf);
        return false;
    }

    /* 4. System Control keys: 0xA5 (KC_PWR) .. 0xA7 (KC_WAKE) */
    if ((uint16_t)(keycode - 0xA5) <= 0x02) {
        uint8_t bit = (uint8_t)(1U << (keycode - 0xA5));
        if (pressed) {
            system_report_buf |= bit;
        } else {
            system_report_buf &= (uint8_t)~bit;
        }
        set_sevent(4, &system_report_buf, 1);
        return false;
    }

    /* 5. Consumer keys: 0xA8 (KC_MUTE) .. 0xC2 */
    if ((uint16_t)(keycode - 0xA8) <= 0x1A) {
        consumer_report_buf = pressed ? CSWTCH_90[(uint8_t)(keycode - 0xA8)] : 0;
        set_sevent(3, (const uint8_t *)&consumer_report_buf, 2);
        return false;
    }

    /* 6. Unhandled keycodes (MO, LT, MT, etc.) -> handled by QMK */
    return true;
}