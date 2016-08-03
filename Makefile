CC = gcc
CFLAGS = -Ofast -g3 -march=native -flto -std=c11 -Wall -Wextra -pedantic #-fprofile-use #-fprofile-generate
CPPFLAGS = -DNDEBUG
LDFLAGS = -fwhole-program
TARGET = brainfuck

# Uncomment to use a fixed-size tape which wraps around at the ends
#CFLAGS += -fwrapv
#CPPFLAGS += -DFIXED_TAPE_SIZE=uint16_t

ifeq ($(CC),gcc)
MMD = -MMD
else
MMD =
endif

all: $(TARGET)

$(TARGET): main.o optimizer.o parser.o brainfuck.o flattener.o interpreter.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(MMD) -c -o $@ $<

clean:
	rm -f -- $(TARGET) *.o *.gch *.gcda *.d

-include *.d

.PHONY: all clean
