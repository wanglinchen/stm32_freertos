# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_VARIANT ?= userdebug
BUILD_DIR := $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/objects
TARGET_DIR := $(BUILD_DIR)/target
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/$(BUILD_VARIANT)/.config

PROJECT_NAME := $(CONFIG_PROJECT_NAME)
ELF := $(TARGET_DIR)/$(PROJECT_NAME).elf
HEX := $(TARGET_DIR)/$(PROJECT_NAME).hex
BIN := $(TARGET_DIR)/$(PROJECT_NAME).bin

.PHONY: all clean modules user userdebug release osl apl fml bsl hal

all:
	@mkdir -p $(BUILD_DIR) $(TARGET_DIR)
	@echo "OPT=$(OPT) DEBUG=$(DEBUG)" > $(BUILD_DIR)/.buildflags
	@$(MAKE) --no-print-directory modules
	@$(MAKE) --no-print-directory $(ELF) $(HEX) $(BIN)
	@echo "SIZE $(ELF)"
	$(SIZE) $(ELF)

modules: osl apl fml bsl hal

$(ELF): $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o $(TARGET_DIR)/lib/libapl.a $(TARGET_DIR)/lib/libfml.a $(TARGET_DIR)/lib/libbsl.a $(TARGET_DIR)/lib/libosl.a $(TARGET_DIR)/lib/libhal.a $(LDSCRIPT) $(PROJECT_ROOT)/toolchain.make
	@mkdir -p $(dir $@)
	@echo "LD $@"
	$(CC) $(LDFLAGS) -o $@ -Wl,--whole-archive $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o -Wl,--no-whole-archive -Wl,--start-group $(TARGET_DIR)/lib/libapl.a $(TARGET_DIR)/lib/libfml.a $(TARGET_DIR)/lib/libbsl.a $(TARGET_DIR)/lib/libosl.a $(TARGET_DIR)/lib/libhal.a -Wl,--end-group $(LDLIBS)

$(HEX): $(ELF)
	@echo "HEX $@"
	$(OBJCOPY) -O ihex $(ELF) $@

$(BIN): $(ELF)
	@echo "BIN $@"
	$(OBJCOPY) -O binary $(ELF) $@

osl apl fml bsl hal:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/$@ all

osl: hal
apl: fml bsl osl
fml: bsl osl
bsl: hal

userdebug:
	@$(MAKE) --no-print-directory -B BUILD_VARIANT=userdebug OPT=-Og DEBUG=-g3 all
user:
	@$(MAKE) --no-print-directory -B BUILD_VARIANT=user OPT=-Os DEBUG=-g0 all
release: user

clean:
	rm -rf $(PROJECT_ROOT)/out/user $(PROJECT_ROOT)/out/userdebug
