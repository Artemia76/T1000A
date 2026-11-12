# Firmware de test T1000-A (Goodix GR5515) — compilation GCC hors arborescence du SDK.
#   make            -> build/t1000a_hello.bin (brut) et build/t1000a_hello_dfu.bin (image DFU)
#   make clean

TARGET   := t1000a_hello
ROOT     := $(abspath ../..)
SDK      ?= $(ROOT)/third_party/gr551x-sdk
BUILD    ?= build
MKIMAGE  := python3 -I $(ROOT)/tools/mkimage.py

SRC_C := \
  src/main.c src/board.c src/safety.c src/console.c \
  $(SDK)/platform/soc/common/gr_system.c \
  $(SDK)/platform/soc/common/gr_interrupt.c \
  $(SDK)/platform/soc/common/gr_platform.c \
  $(SDK)/platform/soc/src/gr_soc.c \
  $(SDK)/drivers/src/app_io.c \
  $(SDK)/drivers/src/app_gpiote.c \
  $(SDK)/drivers/src/app_pwr_mgmt.c \
  $(SDK)/drivers/src/app_aon_wdt.c \
  $(SDK)/components/libraries/utility/utility.c \
  $(SDK)/components/libraries/pmu_calibration/pmu_calibration.c \
  $(SDK)/components/libraries/app_timer/app_timer.c \
  $(SDK)/components/libraries/app_assert/app_assert.c \
  $(SDK)/components/libraries/app_error/app_error.c \
  $(SDK)/components/libraries/app_log/app_log.c \
  $(SDK)/components/sdk/ble.c \
  $(SDK)/components/profiles/common/ble_prf_utils.c \
  $(SDK)/components/profiles/gus/gus.c

SRC_S := $(SDK)/platform/arch/arm/cortex-m/gcc/startup_gr55xx.s

INC := src \
  $(SDK)/components/libraries/app_assert $(SDK)/components/libraries/app_error \
  $(SDK)/components/libraries/app_log $(SDK)/components/libraries/app_timer \
  $(SDK)/components/libraries/hal_flash $(SDK)/components/libraries/pmu_calibration \
  $(SDK)/components/libraries/utility $(SDK)/components/libraries/ring_buffer \
  $(SDK)/components/profiles/common $(SDK)/components/profiles/gus \
  $(SDK)/components/sdk $(SDK)/components/patch/ind \
  $(SDK)/drivers/inc $(SDK)/drivers/inc/hal \
  $(SDK)/external/segger_rtt \
  $(SDK)/platform/arch/arm/cortex-m/cmsis/core/include \
  $(SDK)/platform/boards $(SDK)/platform/include \
  $(SDK)/platform/soc/include $(SDK)/platform/soc/linker/keil

CROSS  := arm-none-eabi-
CC     := $(CROSS)gcc
CPUFLAGS := -mthumb -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16
CFLAGS := -std=gnu99 -Os -g -fno-strict-aliasing $(CPUFLAGS) -ffunction-sections -fdata-sections -MD \
          -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
          $(addprefix -I,$(INC))
LDFLAGS := $(CPUFLAGS) -Wl,--gc-sections -T gr5515_t1000a.lds \
           $(SDK)/platform/soc/linker/gcc/rom_symbol_gcc.txt \
           -L$(SDK)/components/libraries/app_crypto/gcc -lapp_crypto \
           -L$(SDK)/platform/soc/linker/gcc -lble_sdk \
           -Wl,--no-warn-mismatch -Wl,-Map=$(BUILD)/$(TARGET).map

OBJ := $(addprefix $(BUILD)/obj/,$(notdir $(SRC_C:.c=.o) $(SRC_S:.s=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.s $(sort $(dir $(SRC_S)))

all: $(BUILD)/$(TARGET)_dfu.bin

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	@echo "  CC  $(notdir $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.s | $(BUILD)/obj
	@echo "  AS  $(notdir $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ) gr5515_t1000a.lds
	@echo "  LD  $@"
	@$(CC) $(OBJ) $(LDFLAGS) -o $@ 2>&1 | grep -v "Forcing branch to absolute symbol" || true
	@$(CROSS)size $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(CROSS)objcopy -O binary $< $@

$(BUILD)/$(TARGET)_dfu.bin: $(BUILD)/$(TARGET).bin $(ROOT)/tools/mkimage.py
	@$(MKIMAGE) $< $@ --load 0x0101B000 --check-elf $(BUILD)/$(TARGET).elf

$(BUILD)/obj:
	@mkdir -p $@

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
.PHONY: all clean
