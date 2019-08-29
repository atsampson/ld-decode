# Makefile for the Software Decode of Laserdiscs project

# Note: Targets do not include auto-generated .h files, which means
#       that make clean will not remove them.
TARGETS=cx ld-ldf-reader comb-ntsc comb-pal

CFLAGS=-g -O2 -fno-omit-frame-pointer -march=native -Itools/ld-chroma-decoder

all: $(TARGETS)

clean:
	rm -f $(TARGETS)

install:
	cp ld-ldf-reader /usr/local/bin

cx: cx-expander.cxx deemp.h
	clang++ -std=c++14  -Wall $(CFLAGS) -o cx cx-expander.cxx

deemp.h: filtermaker.py
	python3 filtermaker.py > deemp.h

ld-ldf-reader: ld-ldf-reader.c
	clang -o ld-ldf-reader ld-ldf-reader.c -Wno-deprecated-declarations -lavcodec -lavutil -lavformat -O2

comb-ntsc: comb-ntsc.cxx deemp.h
	clang++ -std=c++11  -Wall $(CFLAGS) -o comb-ntsc comb-ntsc.cxx

comb-pal: comb-pal.cxx deemp.h
	clang++ -std=c++11  -Wall $(CFLAGS) -o comb-pal comb-pal.cxx
