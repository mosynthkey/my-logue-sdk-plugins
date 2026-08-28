.PHONY: all unit wasm test site clean

PLUGIN_TARGETS := $(dir $(wildcard plugins/*/targets/*/Makefile))

export GCC_BIN_PATH
export EMCC_BIN_PATH

all: unit

unit:
	@for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir install; \
	done

wasm:
	@for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir wasm-ci; \
	done

test:
	node tests/nts1-midi.test.mjs

site: unit
	bash scripts/assemble-site.sh site

clean:
	@for target_dir in $(PLUGIN_TARGETS); do \
		$(MAKE) -C $$target_dir clean; \
	done
	rm -rf site
