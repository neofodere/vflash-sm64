BUILD ?= build
CC ?= gcc
PYTHON ?= python3
EXEEXT ?=
VERSION ?= dev
LDFLAGS ?=
DECOMP ?= third_party/sm64

REGION ?= us
REGIONS ?= us jp eu
IMAGES ?= $(BUILD)

DECOMP_COPY := $(BUILD)/decomp
R := $(BUILD)/$(REGION)

GAME_DEPS := $(wildcard port/*.c port/*.h port/include/* runtime/*.c runtime/*.S runtime/link.ld \
	runtime/include/* runtime/lib/*.c runtime/lib/*.S runtime/lib/*.h scripts/*.py)

TOOL_SRC := tool/main.c tool/rom.c tool/sha1.c tool/image.c tool/iso.c tool/assets.c tool/sound.c tool/mio0.c tool/lz4.c \
	$(BUILD)/regions.c $(foreach r,$(REGIONS),$(IMAGES)/$(r)/gen/asset_table.c)
TOOL_CFLAGS := -O2 -Wall -Wextra -Itool -DVERSION='"$(VERSION)"'

.PHONY: all cli image test clean

all: cli

image: $(R)/BOOT.BIN

cli: $(BUILD)/vflash-sm64$(EXEEXT)

$(BUILD)/decomp.stamp: scripts/patch_decomp.py
	rm -rf $(DECOMP_COPY)
	mkdir -p $(BUILD)
	cp -r $(DECOMP) $(DECOMP_COPY)
	$(PYTHON) scripts/patch_decomp.py $(DECOMP_COPY)
	touch $@

$(R)/gen/asset_table.c: $(BUILD)/decomp.stamp scripts/gen_assets.py scripts/regions.py
	rm -rf $(R)/gen
	$(PYTHON) scripts/gen_assets.py $(DECOMP_COPY) $(R)/gen $(REGION)

$(BUILD)/lz4c: tool/lz4c.c tool/lz4.c
	@mkdir -p $(BUILD)
	gcc -O2 -Wall -Wextra -Itool -o $@ $^

$(R)/BOOT.BIN: $(R)/gen/asset_table.c $(BUILD)/lz4c $(GAME_DEPS)
	rm -rf $(R)/game
	$(PYTHON) scripts/build_game.py $(DECOMP_COPY) . $(R)/gen $(R)/game $(REGION)
	$(PYTHON) scripts/make_image.py $(R)/game $@ $(BUILD)/lz4c

$(BUILD)/regions.c: scripts/gen_regions.py scripts/regions.py $(foreach r,$(REGIONS),$(IMAGES)/$(r)/BOOT.BIN)
	@mkdir -p $(BUILD)
	$(PYTHON) scripts/gen_regions.py $@ $(IMAGES) $(REGIONS)

$(BUILD)/vflash-sm64$(EXEEXT): $(TOOL_SRC)
	@mkdir -p $(BUILD)
	$(CC) $(TOOL_CFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_tool: tool/test_tool.c tool/rom.c tool/sha1.c tool/assets.c tool/sound.c tool/mio0.c tool/lz4.c
	@mkdir -p $(BUILD)
	$(CC) $(TOOL_CFLAGS) -o $@ $^

test: $(BUILD)/test_tool
	$(BUILD)/test_tool
	sh runtime/lib/test/run.sh

clean:
	rm -rf $(BUILD)
