ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

COMMON_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BUILD_CONFIG_FILE := $(COMMON_DIR)/config.mk
include $(BUILD_CONFIG_FILE)

ENABLE_KEYBOARD_PINYIN_EX ?= 0
ifneq ($(filter $(ENABLE_KEYBOARD_PINYIN_EX),0 1),$(ENABLE_KEYBOARD_PINYIN_EX))
$(error ENABLE_KEYBOARD_PINYIN_EX must be 0 or 1)
endif

OUTPUT_ELF = $(OUTPUT).elf
OUTPUT_BIN = $(OUTPUT).bin

# 遊戲版本變體：BLACK=精灵宝可梦 黑_ver.2.1.8（v7.7.3 定案，symbols.ld）
#              WHITE=精灵宝可梦 白_ver.2.1.8（symbols_white.ld，Δarm9=+0x18 / ov區=+0x20）
GAME_VARIANT ?= BLACK
ifeq ($(GAME_VARIANT),WHITE)
SYMBOLS_LD := $(COMMON_DIR)/symbols_white.ld
VARIANT_DEFS := -DGAME_WHITE=1
else
SYMBOLS_LD := $(COMMON_DIR)/symbols.ld
VARIANT_DEFS :=
endif

PREFIX  := $(DEVKITARM)/bin/arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
AS      := $(PREFIX)as
LD      := $(PREFIX)gcc
CC1     := $(shell $(CC) --print-prog-name=cc1) -quiet
CPP     := $(PREFIX)cpp
AR      := $(PREFIX)ar

OBJ_DIR  := $(COMMON_DIR)/../build

LIBPATH := -L $(DEVKITARM)/../libnds/lib -L $(DEVKITARM)/../calico/lib -L "$(dir $(shell $(CC) -print-file-name=libgcc.a))" -L "$(dir $(shell $(CC) -print-file-name=libnosys.a))" -L "$(dir $(shell $(CC) -print-file-name=libc.a))"
LIBS	:= $(LIBPATH) -lnds9 -lcalico_ds9 -lc -lgcc
NITRO_LIB:= $(OBJ_DIR)/nitro.a

COMMON_INC_DIRS:= $(COMMON_DIR)/lib/include $(COMMON_DIR)/include $(DEVKITARM)/../calico/include $(DEVKITARM)/include $(DEVKITARM)/../libnds/include

NITRO_LIB_SRCS := $(wildcard $(COMMON_DIR)/lib/src/nitro/*.c)
NITRO_LIB_OBJS := $(patsubst $(COMMON_DIR)/lib/%, $(OBJ_DIR)/lib/%, $(NITRO_LIB_SRCS:.c=.o))

NITRO_LIB_COMPILE_MODE := -mthumb -mthumb-interwork
NITRO_LIB_C_FLAGS := -Os -mabi=aapcs -march=armv5te -mtune=arm946e-s -DARM9=1 -D__NDS__ -DNITROSDK_VER=$(NITROSDK_VER)
ifeq ($(IS_NITROSDK_THUMB), 0)
$(OBJ_DIR)/lib/src/nitro/import.o: NITRO_LIB_COMPILE_MODE := -marm
endif

.PHONY: all

all:
	@:

$(NITRO_LIB_OBJS): $(OBJ_DIR)/%.o: $(COMMON_DIR)/%.c
	$(CC) -MMD -MP -MF $(OBJ_DIR)/$*.d  $(foreach dir,$(COMMON_INC_DIRS),-I $(dir)) $(NITRO_LIB_C_FLAGS) $(NITRO_LIB_COMPILE_MODE) -o $(OBJ_DIR)/$*.o -c $<

$(NITRO_LIB): $(NITRO_LIB_OBJS)
	$(AR) rcs $@ $^
