.PHONY: all unit wasm test website clean

PLUGIN_TARGETS := $(dir $(wildcard plugins/*/targets/*/Makefile))

export GCC_BIN_PATH
export EMCC_BIN_PATH

all: unit

unit:
	@set -e; for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir install; \
	done

wasm:
	@set -e; for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir wasm-ci; \
	done

test:
	node tests/nts1-midi.test.mjs

website: unit
	bash scripts/build-website.sh dist/website

clean:
	@for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir clean; \
	done
	rm -rf dist
