# 自动生成，勿手改。刷新：python3 build/build.py update
PROJECT_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BUILD_DIR := $(PROJECT_ROOT)/out/object
TARGET_DIR := $(BUILD_DIR)/target
.DEFAULT_GOAL := all
include $(PROJECT_ROOT)/toolchain.make
-include $(PROJECT_ROOT)/out/.config

PROJECT_NAME := $(CONFIG_PROJECT_NAME)
ELF := $(TARGET_DIR)/$(PROJECT_NAME).elf
HEX := $(TARGET_DIR)/$(PROJECT_NAME).hex
BIN := $(TARGET_DIR)/$(PROJECT_NAME).bin

.PHONY: all clean modules release osl apl fml bsl hal

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

release:
	@$(MAKE) --no-print-directory -B -C $(PROJECT_ROOT) OPT=-Os DEBUG=-g1 all
	@echo "发布构建完成（-Os -g1，尺寸见上面的 SIZE 行）。"
	@echo "注意：3 条指令形态断言在 -Os 下会跳过（osl_reset_cause_capture 早退 / osl_panic_report 重入闸 / iwdg_wait_sync 符号）；"
	@echo "verify --artifacts 会逐条列出被跳过的判据，其余判据照跑。这些机制只在 -Og 下强制，发版前仍需人工核对一遍。"

clean:
	rm -rf $(PROJECT_ROOT)/out
