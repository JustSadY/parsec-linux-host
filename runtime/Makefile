CC ?= cc
OUT ?= ../.cache/runtime
PKGS = jansson libcurl libsystemd openssl
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Werror=implicit-function-declaration -fstack-protector-strong -fPIE -pthread
LDLIBS += $(shell pkg-config --libs $(PKGS)) -pthread
LDFLAGS += -Wl,-z,relro,-z,now -pie
SOURCES := $(wildcard *.c)
OBJECTS := $(patsubst %.c,$(OUT)/%.o,$(SOURCES))
.PHONY: all clean test
all: $(OUT)/parsec-linux-host $(OUT)/hostctl $(OUT)/service-runner
$(OUT):
	mkdir -p $@
$(OUT)/%.o: %.c runtime.h ../native/bridge.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/parsec-linux-host: $(OBJECTS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(OUT)/hostctl $(OUT)/service-runner: $(OUT)/parsec-linux-host
	cp $< $@
$(OUT)/runtime-tests: ../tests/runtime.c $(filter-out $(OUT)/main.o,$(OBJECTS))
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. $(LDFLAGS) $^ $(LDLIBS) -o $@
test: $(OUT)/runtime-tests
	$(OUT)/runtime-tests
clean:
	rm -f $(OBJECTS) $(OUT)/parsec-linux-host $(OUT)/hostctl $(OUT)/service-runner $(OUT)/runtime-tests
