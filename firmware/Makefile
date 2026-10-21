# G30 VESC dashboard firmware, nRF51822 + S130 2.0.1, nRF5 SDK 12.3.0
#
#   make                      single wire build
#   make LINK=fd              two wire (full duplex) build
#   make CFG="-DSTART_OFF=1"  any dash_config.h override
#   make flash_all            erase chip, write SoftDevice + app (OpenOCD)
#
# Paths must not contain spaces, keep them relative.

SDK_ROOT    ?= ../../sdk/SDK12
GNU_PREFIX  ?= arm-none-eabi-
OPENOCD     ?= openocd
OCD_IFACE   ?= interface/stlink.cfg
PYTHON      ?= $(shell command -v python3 2>/dev/null || echo python)

LINK        ?= hd

ifeq ($(LINK),fd)
CFG += -DLINK_FULL_DUPLEX=1
endif

NAME        := g30_vesc_dash_$(LINK)
BUILD       := build/$(LINK)
SD_HEX      := $(SDK_ROOT)/components/softdevice/s130/hex/s130_nrf51_2.0.1_softdevice.hex
LD_SCRIPT   := linker/g30dash_s130_16k.ld

CC          := $(GNU_PREFIX)gcc
OBJCOPY     := $(GNU_PREFIX)objcopy
SIZE        := $(GNU_PREFIX)size

SRC_APP := \
  src/main.c \
  src/dash.c \
  src/display.c \
  src/tm1637.c \
  src/board_io.c \
  src/vesc_link.c \
  src/settings.c \
  src/packet.c \
  src/crc.c \
  src/buffer.c

SRC_SDK := \
  $(SDK_ROOT)/components/toolchain/system_nrf51.c \
  $(SDK_ROOT)/components/softdevice/common/softdevice_handler/softdevice_handler.c \
  $(SDK_ROOT)/components/libraries/timer/app_timer.c \
  $(SDK_ROOT)/components/libraries/util/app_error.c \
  $(SDK_ROOT)/components/libraries/util/app_error_weak.c \
  $(SDK_ROOT)/components/libraries/util/app_util_platform.c \
  $(SDK_ROOT)/components/libraries/util/nrf_assert.c \
  $(SDK_ROOT)/components/libraries/util/sdk_errors.c \
  $(SDK_ROOT)/components/libraries/fstorage/fstorage.c \
  $(SDK_ROOT)/components/ble/common/ble_advdata.c \
  $(SDK_ROOT)/components/ble/common/ble_conn_params.c \
  $(SDK_ROOT)/components/ble/common/ble_srv_common.c \
  $(SDK_ROOT)/components/ble/ble_advertising/ble_advertising.c \
  $(SDK_ROOT)/components/ble/ble_services/ble_nus/ble_nus.c

SRC_ASM := $(SDK_ROOT)/components/toolchain/gcc/gcc_startup_nrf51.S

INC := \
  src \
  $(SDK_ROOT)/components/toolchain \
  $(SDK_ROOT)/components/toolchain/gcc \
  $(SDK_ROOT)/components/toolchain/cmsis/include \
  $(SDK_ROOT)/components/device \
  $(SDK_ROOT)/components/softdevice/s130/headers \
  $(SDK_ROOT)/components/softdevice/s130/headers/nrf51 \
  $(SDK_ROOT)/components/softdevice/common/softdevice_handler \
  $(SDK_ROOT)/components/drivers_nrf/hal \
  $(SDK_ROOT)/components/drivers_nrf/common \
  $(SDK_ROOT)/components/drivers_nrf/delay \
  $(SDK_ROOT)/components/libraries/util \
  $(SDK_ROOT)/components/libraries/timer \
  $(SDK_ROOT)/components/libraries/fstorage \
  $(SDK_ROOT)/components/libraries/log \
  $(SDK_ROOT)/components/libraries/log/src \
  $(SDK_ROOT)/components/libraries/experimental_section_vars \
  $(SDK_ROOT)/components/ble/common \
  $(SDK_ROOT)/components/ble/ble_advertising \
  $(SDK_ROOT)/components/ble/ble_services/ble_nus

DEFS := \
  -DNRF51 -DNRF51822 -DS130 \
  -DSOFTDEVICE_PRESENT -DBLE_STACK_SUPPORT_REQD \
  -DNRF_SD_BLE_API_VERSION=2 -DSWI_DISABLE0 \
  -D__HEAP_SIZE=0 -D__STACK_SIZE=2048

CPU := -mcpu=cortex-m0 -mthumb -mabi=aapcs -mfloat-abi=soft

CFLAGS := $(CPU) $(DEFS) $(CFG) -Os -g3 -std=gnu99 -Wall \
  -ffunction-sections -fdata-sections -fno-strict-aliasing \
  -fno-builtin -fshort-enums -fcommon \
  $(addprefix -I,$(INC))

ASMFLAGS := $(CPU) $(DEFS) -D__STARTUP_CLEAR_BSS -D__START=main -g3 -x assembler-with-cpp

LDFLAGS := $(CPU) -nostartfiles -L$(SDK_ROOT)/components/toolchain/gcc -T$(LD_SCRIPT) \
  -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(NAME).map --specs=nano.specs

LIBS := -lc -lnosys -lm

OBJS := \
  $(addprefix $(BUILD)/app/,$(notdir $(SRC_APP:.c=.o))) \
  $(addprefix $(BUILD)/sdk/,$(notdir $(SRC_SDK:.c=.o))) \
  $(addprefix $(BUILD)/sdk/,$(notdir $(SRC_ASM:.S=.o)))

vpath %.c $(sort $(dir $(SRC_APP) $(SRC_SDK)))
vpath %.S $(dir $(SRC_ASM))

.PHONY: all clean flash flash_all erase backup test

all: $(BUILD)/$(NAME).hex $(BUILD)/$(NAME)_with_sd.hex

$(BUILD)/app/%.o: %.c src/dash_config.h src/board_g30.h | $(BUILD)/app
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sdk/%.o: %.c | $(BUILD)/sdk
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/sdk/%.o: %.S | $(BUILD)/sdk
	$(CC) $(ASMFLAGS) -c $< -o $@

$(BUILD)/$(NAME).out: $(OBJS) $(LD_SCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@
	$(SIZE) $@

$(BUILD)/$(NAME).hex: $(BUILD)/$(NAME).out
	$(OBJCOPY) -O ihex $< $@

# SoftDevice + application in one file
$(BUILD)/$(NAME)_with_sd.hex: $(BUILD)/$(NAME).hex
	$(PYTHON) tools/mergehex.py $(SD_HEX) $< $@

$(BUILD)/app $(BUILD)/sdk:
	mkdir -p $@

clean:
	rm -rf build

# Try to save the stock flash first. Reads back zeros if the chip is
# read protected, then a backup is not possible.
backup:
	$(OPENOCD) -f $(OCD_IFACE) -f target/nrf51.cfg -c "init" -c "halt" 	  -c "dump_image stock_flash.bin 0x0 0x40000" -c "dump_image stock_uicr.bin 0x10001000 0x400" -c "exit"

erase:
	$(OPENOCD) -f $(OCD_IFACE) -f target/nrf51.cfg -c "init" -c "halt" -c "nrf5 mass_erase" -c "exit"

flash: $(BUILD)/$(NAME).hex
	$(OPENOCD) -f $(OCD_IFACE) -f target/nrf51.cfg -c "program $< verify reset exit"

flash_all: $(BUILD)/$(NAME)_with_sd.hex
	$(OPENOCD) -f $(OCD_IFACE) -f target/nrf51.cfg -c "init" -c "halt" -c "nrf5 mass_erase" \
	  -c "program $< verify reset exit"

# Host simulation tests (native gcc), both link modes
HOST_CC   ?= gcc
TEST_SRC  := src/vesc_link.c src/dash.c src/display.c src/packet.c src/crc.c \
             src/buffer.c test/mock_hw.c test/test_main.c
TEST_FLAGS := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -O1 -g -Itest/mock -Isrc -Itest

test:
	mkdir -p build
	$(HOST_CC) $(TEST_FLAGS) $(TEST_SRC) -lm -o build/test_hd
	$(HOST_CC) $(TEST_FLAGS) -DLINK_FULL_DUPLEX=1 $(TEST_SRC) -lm -o build/test_fd
	./build/test_hd
	./build/test_fd
