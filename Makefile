CC = gcc
CFLAGS = -Ofast -g3 -march=native -flto -std=c11 -Wall -Wextra -pedantic #-fprofile-use #-fprofile-generate
LDFLAGS = -fwhole-program
TARGET = brainfuck

all: $(TARGET)

$(TARGET): main.o optimizer.o parser.o brainfuck.o flattener.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -c -o $@ $<

clean:
	rm -f -- $(TARGET) *.o *.gch *.d

-include *.d

.PHONY: all clean
