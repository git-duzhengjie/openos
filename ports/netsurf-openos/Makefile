CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS += -Iinclude
BUILD_DIR := build
SMOKE := $(BUILD_DIR)/netsurf-openos-smoke

.PHONY: all smoke clean

all: smoke

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(SMOKE): src/openos_netsurf_host.c src/smoke.c include/openos_netsurf_platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/openos_netsurf_host.c src/smoke.c -o $@

smoke: $(SMOKE)
	./$(SMOKE)

clean:
	rm -rf $(BUILD_DIR)
