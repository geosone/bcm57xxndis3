WATCOM ?= /opt/watcom
export WATCOM
export PATH := $(WATCOM)/binl64:$(PATH)
CC386 ?= $(WATCOM)/binl64/wcc386
ASM ?= jwasm
LINK386 ?= jwlink
HOST_CC ?= cc
PYTHON ?= python3
OUT := build
CFLAGS386 := -q -3s -mf -ecc -s -zl -zls -ox -w4 -Isrc -I$(WATCOM)/h

.PHONY: all check smoke network-fixture clean
all: $(OUT)/BCM57DBG.386

$(OUT):
	mkdir -p $@

$(OUT)/entry.obj: src/entry.asm | $(OUT)
	$(ASM) -q -Fo$@ $<

$(OUT)/%.obj: src/%.c src/pci.h src/ndis3.h src/platform.h src/bcm5722_hw.h | $(OUT)
	$(CC386) $(CFLAGS386) -fo=$@ $<

$(OUT)/bcm5722_hw.obj: src/bcm5722_hw.h

$(OUT)/BCM57DBG.386: $(OUT)/entry.obj $(OUT)/probe.obj $(OUT)/pci.obj $(OUT)/ndis3.obj $(OUT)/bcm5722_hw.obj $(OUT)/platform.obj tools/check_vxd.py
	$(LINK386) format windows vxd name $@ option quiet option map=$(OUT)/BCM57DBG.map file $(OUT)/entry.obj file $(OUT)/probe.obj file $(OUT)/pci.obj file $(OUT)/ndis3.obj file $(OUT)/bcm5722_hw.obj file $(OUT)/platform.obj segment _TEXT PRELOAD NONDISCARDABLE segment _DATA PRELOAD NONDISCARDABLE export BCM57DBG_DDB.1=_BCM57DBG_DDB
	$(PYTHON) tools/check_vxd.py --prepare $@

$(OUT)/test_pci: tests/test_pci.c src/pci.c src/pci.h | $(OUT)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -pedantic -Isrc tests/test_pci.c src/pci.c -o $@

$(OUT)/test_ndis3: tests/test_ndis3.c src/ndis3.c src/ndis3.h | $(OUT)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -pedantic -Isrc tests/test_ndis3.c src/ndis3.c -o $@

$(OUT)/test_hw: tests/test_hw.c src/bcm5722_hw.c src/bcm5722_hw.h | $(OUT)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -pedantic -Isrc tests/test_hw.c src/bcm5722_hw.c -o $@

$(OUT)/test_platform: tests/test_platform.c src/platform.c src/platform.h src/bcm5722_hw.h | $(OUT)
	$(HOST_CC) -std=c99 -Wall -Wextra -Werror -pedantic -Isrc tests/test_platform.c src/platform.c -o $@

check: all $(OUT)/test_pci $(OUT)/test_ndis3 $(OUT)/test_hw $(OUT)/test_platform
	$(OUT)/test_pci
	$(OUT)/test_ndis3
	$(OUT)/test_hw
	$(OUT)/test_platform
	$(PYTHON) tools/check_vxd.py $(OUT)/BCM57DBG.386

$(OUT)/WFWEXIT.EXE: tests/wfw_exit.c | $(OUT)
	$(WATCOM)/binl64/wcc -q -bt=windows -ms -0 -os -I$(WATCOM)/h -I$(WATCOM)/h/win -fo=$(OUT)/wfw_exit.obj $<
	$(WATCOM)/binl64/wlink option quiet system windows option stack=4k,heapsize=1k name $@ file $(OUT)/wfw_exit.obj

smoke: all $(OUT)/WFWEXIT.EXE
	$(PYTHON) tools/smoke_wfw.py "$(WFW_IMAGE)" $(if $(NDIS_IMAGE),--ndis "$(NDIS_IMAGE)") $(if $(VNETSUP_IMAGE),--vnetsup "$(VNETSUP_IMAGE)") $(if $(IFSHLP_IMAGE),--ifshlp "$(IFSHLP_IMAGE)") $(if $(NETWORK_DIR),--network-dir "$(NETWORK_DIR)")

$(OUT)/expand_kwaj: tools/expand_kwaj.c | $(OUT)
	$(HOST_CC) -Wall -Wextra -Werror $< -lmspack -o $@

network-fixture: $(OUT)/expand_kwaj
	$(PYTHON) tools/prepare_wfw_network.py "$(WFW_MEDIA)" $(OUT)/wfw-network $(if $(NDIS_CPU_FIX),--ndis-cpu-fix)

clean:
	rm -rf $(OUT)
