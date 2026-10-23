# Dyna Blaster native port - Apple Silicon (arm64) build with clang + raylib
CC      ?= clang
ARCH    ?= -arch arm64
BREW    := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
RAYLIB  ?= $(shell brew --prefix raylib 2>/dev/null || echo $(BREW)/opt/raylib)
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS  += $(ARCH) -I$(RAYLIB)/include
LDFLAGS += $(ARCH) $(RAYLIB)/lib/libraylib.a \
           -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL \
           -framework QuartzCore -framework Metal -framework CoreAudio -framework AudioToolbox
SRC := src/main.c src/engine.c src/game.c src/audio.c third_party/nuked/opl3.c
OBJ := $(SRC:%.c=build/%.o)
BIN := dynablaster

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: %.c src/engine.h src/game.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) ../dyna

clean:
	rm -rf build $(BIN)

.PHONY: all run clean test dmg

test: build/src/engine.o build/src/game.o build/src/audio.o build/third_party/nuked/opl3.o
	$(CC) $(CFLAGS) tests/pwtest.c $^ -o build/pwtest $(LDFLAGS)
	./build/pwtest ../dyna

dmg: $(BIN)
	sh packaging/make_dmg.sh
