CC = gcc
CFLAGS = -Ofast -g3 -march=native -flto -std=c11 -Wall -Wextra -pedantic #-fprofile-use #-fprofile-generate
LDFLAGS = -fwhole-program
TARGET = brainfuck

all: $(TARGET)

# GCC-only things:
optimizer.o: optimizer.gch
parser.o: parser.gch

%.gch: %.h
	gcc -o $@ $<
# END GCC-only things

$(TARGET): main.o optimizer.o parser.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f -- $(TARGET) *.o *.gch

.PHONY: all clean
