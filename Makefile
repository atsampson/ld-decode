### Top-level Makefile for ld-decode ###

# Prefix into which ld-decode will be installed.
# This must be set both at build and install time. If you're using a
# non-default directory here, make sure that Python knows to look in there for
# modules (e.g. by setting PYTHONPATH in your environment).
prefix ?= /usr/local

# Staging dir for building distribution packages.
# If you're building packages, it may make more sense to build the Python and
# Qt parts using your distribution's tools, rather than using this Makefile
# (but don't forget the helpers!).
DESTDIR =

# Tools you might want to override
PYTHON3 ?= python3
QMAKE ?= qmake

### Targets for users to invoke ###

all: build-helpers build-python build-tools
check: check-python check-tools
install: install-helpers install-python install-tools
clean: clean-helpers clean-python clean-tools

.PHONY: all build-helpers build-python build-tools
.PHONY: check check-python check-tools
.PHONY: install install-helpers install-python install-tools
.PHONY: clean clean-helpers clean-python clean-tools

### Helper programs used by ld-decode ###

helpers = ld-ldf-reader

build-helpers: $(helpers)

ld-ldf-reader: ld-ldf-reader.c
	$(CC) -O2 -Wno-deprecated-declarations -o $@ $< -lavcodec -lavutil -lavformat

install-helpers:
	install -d "$(DESTDIR)$(prefix)/bin"
	install -m755 $(helpers) "$(DESTDIR)$(prefix)/bin"

clean-helpers:
	rm -f $(helpers)

### Python modules and scripts ###

build-python:
	$(PYTHON3) setup.py build

check-python:
	@if [ ! -f testdata/ve-snw-cut.lds ]; then \
		echo "You must git clone ld-decode-testdata into 'testdata'"; \
		exit 1; \
	fi

	@echo ">>> Decoding NTSC CAV"
	scripts/test-decode \
		--decoder mono --decoder ntsc2d --decoder ntsc3d \
		--expect-frames 29 \
		--expect-bpsnr 43.3 \
		--expect-vbi 9151563,15925840,15925840 \
		--expect-efm-samples 40572 \
		testdata/ve-snw-cut.lds

	@echo ">>> Decoding NTSC CLV"
	scripts/test-decode \
		--expect-frames 4 \
		--expect-bpsnr 37.6 \
		--expect-vbi 9167913,15785241,15785241 \
		testdata/issues/176/issue176.lds

	@echo ">>> Decoding PAL CAV"
	scripts/test-decode --pal \
		--decoder mono --decoder pal2d --decoder transform2d --decoder transform3d \
		--expect-frames 4 \
		--expect-bpsnr 40.6 \
		--expect-vbi 9151527,16065688,16065688 \
		--expect-efm-samples 5292 \
		testdata/pal/jason-testpattern.lds

	@echo ">>> Decoding PAL CLV"
	scripts/test-decode --pal --no-efm \
		--expect-frames 9 \
		--expect-bpsnr 31.8 \
		--expect-vbi 0,8449774,8449774 \
		testdata/pal/kagemusha-leadout-cbar.ldf

install-python:
	if [ -z "$(DESTDIR)" ]; then \
		$(PYTHON3) setup.py install --prefix="$(prefix)"; \
	else \
		$(PYTHON3) setup.py install --root="$(DESTDIR)" --prefix="$(prefix)"; \
	fi

clean-python:
	$(PYTHON3) setup.py clean -a

### Qt-based tools ###

build-tools:
	cd tools && $(QMAKE) -recursive PREFIX="$(prefix)"
	$(MAKE) -C tools

check-tools:
	tools/library/filter/testfilter/testfilter

install-tools:
	$(MAKE) -C tools install INSTALL_ROOT="$(DESTDIR)"

clean-tools:
	$(MAKE) -C tools clean

### Generated files, not updated automatically ###

tools/library/filter/deemp.h: scripts/filtermaker
	$(PYTHON3) scripts/filtermaker >$@
