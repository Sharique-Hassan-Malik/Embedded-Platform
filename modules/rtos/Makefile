##
## Makefile — CortexRTOS for STM32F401RE Nucleo
##
## Targets:
##   all       — build firmware ELF + binary
##   flash     — flash via OpenOCD (st-link)
##   clean     — remove build artefacts
##   size      — show section sizes
##   test      — run host-side unit tests (no hardware required)
##

CROSS  := arm-none-eabi-
CC     := $(CROSS)gcc
AS     := $(CROSS)as
LD     := $(CROSS)ld
OBJCOPY:= $(CROSS)objcopy
SIZE   := $(CROSS)size

BUILD  := build
TARGET := $(BUILD)/cortex-rtos

##── Source files ────────────────────────────────────────────────────────────────

SRCS_C := kernel/src/kernel.c    \
           kernel/src/scheduler.c \
           kernel/src/mutex.c     \
           kernel/src/semaphore.c \
           kernel/src/pool.c      \
           port/cortex-m4/port.c  \
           hal/src/hal.c          \
           app/main.c

SRCS_S := port/cortex-m4/port_asm.s \
           tools/startup_stm32f401.s

OBJS   := $(patsubst %.c,$(BUILD)/%.o,$(SRCS_C)) \
          $(patsubst %.s,$(BUILD)/%.o,$(SRCS_S))

##── Include paths ───────────────────────────────────────────────────────────────

INCS := -Ikernel/include \
        -Ihal/include

##── Compiler and linker flags ───────────────────────────────────────────────────

CPU_FLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

CFLAGS := $(CPU_FLAGS)             \
          $(INCS)                  \
          -DCORTEX_M               \
          -DRTOS_CORE_CLOCK_HZ=84000000UL \
          -DRTOS_TICK_HZ=1000      \
          -Os                      \
          -Wall -Wextra            \
          -ffunction-sections      \
          -fdata-sections          \
          -std=c11

ASFLAGS := $(CPU_FLAGS)

LDFLAGS := $(CPU_FLAGS)                          \
           -T tools/stm32f401re.ld               \
           -Wl,--gc-sections                     \
           -Wl,-Map,$(TARGET).map                \
           -nostdlib                             \
           -specs=nano.specs                     \
           -lc -lm -lnosys

##── Rules ────────────────────────────────────────────────────────────────────────

.PHONY: all clean flash size test

all: $(TARGET).bin

$(TARGET).elf: $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(OBJS) $(LDFLAGS) -o $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

size: $(TARGET).elf
	$(SIZE) $<

flash: $(TARGET).bin
	openocd -f interface/stlink.cfg \
	        -f target/stm32f4x.cfg  \
	        -c "program $(TARGET).bin 0x08000000 verify reset exit"

clean:
	rm -rf $(BUILD)

##── Host-side unit tests (no cross-compiler needed) ─────────────────────────────

test:
	$(MAKE) -C tests/host
