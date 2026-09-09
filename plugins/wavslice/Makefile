##############################################################################
# WavSlice helpers (embed a WAV, build the NTS-3 unit)
#

PLUGIN_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

WAV ?=
BPM ?=
START ?= 0
BARS ?= 1
GCC_BIN_PATH ?=
EMCC_BIN_PATH ?=

EMBED_CMD = python3 $(PLUGIN_ROOT)/scripts/embed_wav.py \
	--out $(PLUGIN_ROOT)/dsp/wavslice_pcm.h \
	--start $(START) \
	--bars $(BARS)

ifneq ($(strip $(WAV)),)
EMBED_CMD += --wav $(WAV) --write-loop $(PLUGIN_ROOT)/assets/loop.wav
endif
ifneq ($(strip $(BPM)),)
EMBED_CMD += --bpm $(BPM)
endif

.PHONY: all embed default-loop install wasm test clean

all: install

default-loop:
	python3 $(PLUGIN_ROOT)/scripts/make_default_loop.py \
		--out $(PLUGIN_ROOT)/assets/default-loop.wav

embed:
	$(EMBED_CMD)

install: embed
	$(MAKE) -C $(PLUGIN_ROOT)/targets/nts-3_kaoss install \
		$(if $(GCC_BIN_PATH),GCC_BIN_PATH=$(GCC_BIN_PATH))

wasm: embed
	$(MAKE) -C $(PLUGIN_ROOT)/targets/nts-3_kaoss wasm-ci \
		$(if $(EMCC_BIN_PATH),EMCC_BIN_PATH=$(EMCC_BIN_PATH))

test: embed
	g++ -O2 -std=c++11 \
		-I $(PLUGIN_ROOT)/dsp \
		-I $(PLUGIN_ROOT)/../common \
		-I $(PLUGIN_ROOT)/../../third_party/logue-sdk/platform/nts-3_kaoss/common \
		-I $(PLUGIN_ROOT)/../../third_party/logue-sdk/platform/nts-3_kaoss \
		$(PLUGIN_ROOT)/scripts/render_offline_test.cc \
		-o /tmp/wavslice_test
	/tmp/wavslice_test

clean:
	$(MAKE) -C $(PLUGIN_ROOT)/targets/nts-3_kaoss clean
	rm -f $(PLUGIN_ROOT)/dsp/wavslice_pcm.h
