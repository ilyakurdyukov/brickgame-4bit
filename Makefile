
CFLAGS = -O2 -Wall -Wextra -std=c99 -pedantic -Wno-unused
APPNAME = brickgame
ROMNAME = brickrom.bin
DECOMPILED = 0

.PHONY: all clean
all: $(APPNAME)

ifeq ($(DECOMPILED),1)
clean:
	$(RM) $(APPNAME) ht4bit_decomp brickgame_dec.c

ht4bit_decomp: ht4bit_decomp.c
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)

brickgame_dec.c: ht4bit_decomp
	./ht4bit_decomp --rom "$(ROMNAME)" -o brickgame_dec.c

$(APPNAME): $(APPNAME).c brickgame_dec.c
	$(CC) -s $(filter-out -pedantic,$(CFLAGS)) -DDECOMPILED=1 -o $@ $< $(LIBS)
else
clean:
	$(RM) $(APPNAME)

$(APPNAME): $(APPNAME).c
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)
endif

