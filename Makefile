CC = gcc
CFLAGS = -Ofast -s -march=native -flto -std=c11 -Wall -Wextra -pedantic -fweb #-fprofile-use #-fprofile-generate
CPPFLAGS = -DNDEBUG
LDFLAGS = -fwhole-program
TARGET = brainfuck
TARGET2C = brainfuck2c

# Uncomment to use a fixed-size tape which wraps around at the ends
#CPPFLAGS += -DFIXED_TAPE_SIZE=uint16_t

ifeq ($(CC),gcc)
MMD = -MMD
else
MMD =
endif

all: $(TARGET) $(TARGET2C)

$(TARGET): main.o optimizer.o parser.o brainfuck.o debug.o optimizer_helpers.o flattener.o interpreter.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

$(TARGET2C): main.o optimizer.o parser.o brainfuck.o debug.o optimizer_helpers.o flattener.o interpreter_output_c.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MMD) -c -o $@ $<

clean:
	rm -f -- $(TARGET) $(TARGET2C) *.o *.gch *.gcda *.d

-include *.d

.PHONY: all clean
