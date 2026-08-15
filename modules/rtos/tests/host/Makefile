##
## tests/host/Makefile — compile and run kernel unit tests with native GCC.
##
## No cross-compiler required.  The kernel C sources are compiled with
## -UCORTEX_M so all assembly-gated blocks are excluded.  Port and hardware
## functions are replaced by host_stubs.c.
##

CC     := gcc
CFLAGS := -std=c11 -Wall -Wextra -g \
          -UCORTEX_M \
          -DRTOS_MAX_TASKS=8 \
          -DRTOS_TICK_HZ=1000 \
          -DRTOS_CORE_CLOCK_HZ=84000000 \
          -DRTOS_HEAP_BYTES=4096 \
          -DRTOS_DEFAULT_STACK_BYTES=512 \
          -DRTOS_PRIORITY_LEVELS=8 \
          -I../../kernel/include

KERNEL_SRCS := ../../kernel/src/kernel.c    \
               ../../kernel/src/scheduler.c \
               ../../kernel/src/mutex.c     \
               ../../kernel/src/semaphore.c \
               ../../kernel/src/pool.c

TEST_SRCS := test_kernel.c host_stubs.c

TARGET := test_kernel

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(KERNEL_SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
