CC      := arm-none-eabi-gcc
CFLAGS  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
           -O2 -Wall -Wextra -Wno-unused-parameter \
           -ffreestanding -ffunction-sections -fdata-sections \
           -Ifirmware/include
LDFLAGS := -Lfirmware/src -Tfirmware/src/stm32f401.ld \
           -nostartfiles -Wl,--gc-sections
LDLIBS  := -lm
TARGET  := foc_motor

SRCS := firmware/src/foc.c firmware/src/hal.c firmware/src/main.c
OBJS := $(SRCS:firmware/src/%.c=build/%.o) build/startup_stm32f401.o

.PHONY: all clean flash size test

all: build/$(TARGET).elf

build/%.o: firmware/src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: firmware/src/%.s | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(TARGET).elf: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/$(TARGET).bin: build/$(TARGET).elf
	arm-none-eabi-objcopy -O binary $< $@

build:
	mkdir -p build

size: build/$(TARGET).elf
	arm-none-eabi-size $<

flash: build/$(TARGET).bin
	st-flash write $< 0x08000000

test:
	python -m pytest tests/test_foc.py -v

clean:
	rm -rf build
