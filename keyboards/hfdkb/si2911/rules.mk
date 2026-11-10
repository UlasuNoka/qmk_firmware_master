ifeq ($(strip $(CONSOLE_ENABLE)), yes)
    KEYBOARD_SHARED_EP = no
endif

RULES_MK_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

OPT_DEFS += -DMULTIMODE_ENABLE
OPT_DEFS += -DENTRY_STOP_MODE
OPT_DEFS += -DNO_USB_STARTUP_CHECK
# OPT_DEFS += -DWWDG_ENABLE

UART_DRIVER_REQUIRED = yes

SRC += common/bt_task.c
SRC += common/retarget_suspend.c
SRC += common/lp_sleep.c
# SRC += common/wb32_wwdg.c
# SRC += common/wwdg.c
SRC += bled/bled.c

ifeq ($(strip $(USE_OPEN_BTS)), yes)
    SRC += common/bts_lib.c
else
    LDFLAGS += -L $(RULES_MK_DIR)/common -l_bts
endif

VPATH += $(RULES_MK_DIR)/common
VPATH += $(RULES_MK_DIR)/bled
