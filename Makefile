CC ?= cc
CFLAGS ?= -O2

watchdog_objects := build/main.o build/signal_manager.o build/process_manager.o build/logger.o
worker_objects := build/worker.o
compile_flags := -std=c11 -Wall -Wextra -Wpedantic
project_cppflags := -Iinclude -D_POSIX_C_SOURCE=200809L

.PHONY: all clean test

all: watchdog workers/worker

watchdog: $(watchdog_objects)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

workers/worker: $(worker_objects)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/%.o: src/%.c | build
	$(CC) $(CPPFLAGS) $(project_cppflags) $(CFLAGS) $(compile_flags) -MMD -MP -c $< -o $@

build/worker.o: workers/worker.c | build
	$(CC) $(CPPFLAGS) $(project_cppflags) $(CFLAGS) $(compile_flags) -MMD -MP -c $< -o $@

build:
	mkdir -p $@

test: all
	bash tests/test_scenarios.sh

clean:
	$(RM) -r build watchdog workers/worker

-include $(watchdog_objects:.o=.d) $(worker_objects:.o=.d)
