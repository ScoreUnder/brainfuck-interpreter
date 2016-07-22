CC = gcc
CFLAGS = -Ofast -g3 -march=native -flto -fwhole-program -std=c11 -Wall -Wextra -pedantic #-fprofile-use #-fprofile-generate
TARGET = brainfuck

all: $(TARGET)

$(TARGET): main.o
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f -- $(TARGET) *.o

.PHONY: all clean
