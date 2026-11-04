# 自动生成，勿手改。刷新：python3 build/create-project.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(PROJECT_ROOT)/out/object
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/.config

PROJECT_NAME := $(CONFIG_PROJECT_NAME)
ELF := $(BUILD_DIR)/$(PROJECT_NAME).elf
HEX := $(BUILD_DIR)/$(PROJECT_NAME).hex
BIN := $(BUILD_DIR)/$(PROJECT_NAME).bin

.PHONY: all clean osl apl fml bsl hal
all: osl apl fml bsl hal
	@mkdir -p $(dir $(ELF))
	@echo "LD $(ELF)"
	$(CC) $(LDFLAGS) -o $(ELF) -Wl,--whole-archive $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o -Wl,--no-whole-archive $(BUILD_DIR)/lib/libhal.a $(BUILD_DIR)/lib/libosl.a $(BUILD_DIR)/lib/libapl.a $(BUILD_DIR)/lib/libfml.a $(BUILD_DIR)/lib/libbsl.a $(LDLIBS)
	@echo "HEX $(HEX)"
	$(OBJCOPY) -O ihex $(ELF) $(HEX)
	@echo "BIN $(BIN)"
	$(OBJCOPY) -O binary $(ELF) $(BIN)
	@echo "SIZE $(ELF)"
	$(SIZE) $(ELF)

osl apl fml bsl hal:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/$@ all

osl: hal
apl: fml osl
fml: bsl hal osl
bsl: hal

clean:
	rm -rf $(PROJECT_ROOT)/out
