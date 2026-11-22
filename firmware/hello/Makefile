# Firmware de test T1000-A (Goodix GR5515) — compilation GCC hors arborescence du SDK.
#   make            -> build/t1000a_hello.bin (brut) et build/t1000a_hello_dfu.bin (image DFU)
#   make test       -> recompile puis valide dans l'émulateur (tools/sim)
#   make clean

TARGET   := t1000a_hello
ROOT     := $(abspath ../..)
PYTHON   ?= $(firstword $(wildcard $(ROOT)/.venv/bin/python) python3)
SDK      ?= $(ROOT)/third_party/gr551x-sdk
BUILD    ?= build
MKIMAGE  := python3 -I $(ROOT)/tools/mkimage.py

RADIOLIB ?= $(ROOT)/third_party/radiolib/src

SRC_CPP := src/radio_hal.cpp src/radio_test.cpp \
  $(RADIOLIB)/Module.cpp $(RADIOLIB)/Hal.cpp \
  $(RADIOLIB)/modules/LR11x0/LR11x0.cpp $(RADIOLIB)/modules/LR11x0/LR11x0_commands.cpp \
  $(RADIOLIB)/modules/LR11x0/LR_common.cpp $(RADIOLIB)/modules/LR11x0/LR1110.cpp \
  $(RADIOLIB)/modules/LR11x0/LR11x0_wifi.cpp $(RADIOLIB)/modules/LR11x0/LR11x0_gnss.cpp \
  $(RADIOLIB)/modules/LR11x0/LR11x0_crypto.cpp \
  $(RADIOLIB)/protocols/PhysicalLayer/PhysicalLayer.cpp \
  $(RADIOLIB)/utils/Utils.cpp $(RADIOLIB)/utils/CRC.cpp

SRC_C := \
  src/main.c src/board.c src/safety.c src/console.c src/diag.c \
  $(SDK)/platform/soc/common/gr_system.c \
  $(SDK)/platform/soc/common/gr_interrupt.c \
  $(SDK)/platform/soc/common/gr_platform.c \
  $(SDK)/platform/soc/src/gr_soc.c \
  $(SDK)/drivers/src/app_io.c \
  $(SDK)/drivers/src/app_gpiote.c \
  $(SDK)/drivers/src/app_pwr_mgmt.c \
  $(SDK)/drivers/src/app_aon_wdt.c \
  $(SDK)/drivers/src/app_spi.c \
  $(SDK)/drivers/src/app_dma.c \
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

INC := src $(RADIOLIB) $(ROOT)/platform/gr551x/variants/t1000-a \
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
CXX    := $(CROSS)g++
SHELL  := /bin/bash
CPUFLAGS := -mthumb -mcpu=cortex-m4 -mfloat-abi=softfp -mfpu=fpv4-sp-d16
CFLAGS := -std=gnu99 -Os -g -fno-strict-aliasing $(CPUFLAGS) -ffunction-sections -fdata-sections -MD \
          -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-format \
          $(addprefix -I,$(INC))
CXXFLAGS := -std=gnu++17 -Os -g -fno-strict-aliasing $(CPUFLAGS) -ffunction-sections -fdata-sections -MD \
          -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit \
          -Wall -Wno-unused-function -Wno-unused-variable -Wno-format -Wno-sign-compare \
          -Wno-missing-field-initializers -Wno-register -Wno-volatile \
          -DRADIOLIB_STATIC_ONLY=1 -DRADIOLIB_LOW_LEVEL=1 $(addprefix -I,$(INC))
LDFLAGS := $(CPUFLAGS) -specs=nosys.specs -Wl,--gc-sections -T gr5515_t1000a.lds \
           $(SDK)/platform/soc/linker/gcc/rom_symbol_gcc.txt \
           -L$(SDK)/components/libraries/app_crypto/gcc -lapp_crypto \
           -L$(SDK)/platform/soc/linker/gcc -lble_sdk \
           -Wl,-Map=$(BUILD)/$(TARGET).map

OBJ := $(addprefix $(BUILD)/obj/,$(notdir $(SRC_C:.c=.o) $(SRC_CPP:.cpp=.o) $(SRC_S:.s=.o)))
vpath %.c $(sort $(dir $(SRC_C)))
vpath %.cpp $(sort $(dir $(SRC_CPP)))
vpath %.s $(sort $(dir $(SRC_S)))

all: $(BUILD)/$(TARGET)_dfu.bin

$(BUILD)/obj/%.o: %.c | $(BUILD)/obj
	@echo "  CC  $(notdir $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.cpp | $(BUILD)/obj
	@echo "  CXX $(notdir $<)"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.s | $(BUILD)/obj
	@echo "  AS  $(notdir $<)"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ) gr5515_t1000a.lds
	@echo "  LD  $@"
	@$(CXX) $(OBJ) $(LDFLAGS) -o $@ > $(BUILD)/ld.log 2>&1; st=$$?; \
	  grep -v -e "Forcing branch to absolute symbol" -e "is not implemented and will always fail" \
	          -e "does not take linker garbage collection" -e "in function \`_" $(BUILD)/ld.log; \
	  if [ $$st -ne 0 ]; then rm -f $@; exit $$st; fi
	@$(CROSS)size $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	@$(CROSS)objcopy -O binary $< $@

$(BUILD)/$(TARGET)_dfu.bin: $(BUILD)/$(TARGET).bin $(ROOT)/tools/mkimage.py
	@$(MKIMAGE) $< $@ --load 0x0101B000 --check-elf $(BUILD)/$(TARGET).elf

$(BUILD)/obj:
	@mkdir -p $@

# Validation dans l'émulateur GR5515 (recompile d'abord : jamais de test sur un ELF périmé)
test: $(BUILD)/$(TARGET)_dfu.bin
	$(PYTHON) -I $(ROOT)/emulator/twin/legacy/check_hello.py $(BUILD)/$(TARGET).elf

clean:
	rm -rf $(BUILD)

-include $(OBJ:.o=.d)
.PHONY: all clean test
