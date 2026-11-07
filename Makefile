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

.PHONY: all clean modules osl apl fml bsl hal

all:
	@$(MAKE) --no-print-directory modules
	@$(MAKE) --no-print-directory $(ELF) $(HEX) $(BIN)
	@echo "SIZE $(ELF)"
	$(SIZE) $(ELF)

modules: osl apl fml bsl hal

$(ELF): $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o $(BUILD_DIR)/lib/libapl.a $(BUILD_DIR)/lib/libfml.a $(BUILD_DIR)/lib/libbsl.a $(BUILD_DIR)/lib/libosl.a $(BUILD_DIR)/lib/libhal.a $(LDSCRIPT)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	$(CC) $(LDFLAGS) -o $@ -Wl,--whole-archive $(BUILD_DIR)/hal/chip/startup/src/startup_stm32f429xx.o -Wl,--no-whole-archive -Wl,--start-group $(BUILD_DIR)/lib/libapl.a $(BUILD_DIR)/lib/libfml.a $(BUILD_DIR)/lib/libbsl.a $(BUILD_DIR)/lib/libosl.a $(BUILD_DIR)/lib/libhal.a -Wl,--end-group $(LDLIBS)

$(HEX): $(ELF)
	@echo "HEX $@"
	$(OBJCOPY) -O ihex $(ELF) $@

$(BIN): $(ELF)
	@echo "BIN $@"
	$(OBJCOPY) -O binary $(ELF) $@

osl apl fml bsl hal:
	@$(MAKE) --no-print-directory -C $(PROJECT_ROOT)/$@ all

osl: hal
apl: fml osl
fml: bsl hal osl
bsl: hal

clean:
	rm -rf $(PROJECT_ROOT)/out
