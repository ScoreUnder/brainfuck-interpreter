#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include "interpreter.h"
#include "brainfuck.h"

#ifdef FIXED_TAPE_SIZE
typedef struct {
	FIXED_TAPE_SIZE pos;
	cell_int *restrict cells;
} tape_struct;
#else
typedef struct {
	size_t back_size;  // Amount of tape corresponding to negative offsets (used for smarter reallocation purposes)
	size_t front_size; // Amount of tape corresponding to positive offsets (used for smarter reallocation purposes)
	size_t pos;
	cell_int *restrict cells;
#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
	size_t bound_upper, bound_lower;
#endif
} tape_struct;
#endif

#ifndef FIXED_TAPE_SIZE
inline static void tape_ensure_space(tape_struct *restrict tape, ssize_t pos) {
	if (pos < 0) {
		size_t old_back_size = tape->back_size;
		while (pos < 0) {
			pos += tape->back_size;
			tape->back_size *= 2;
		}
		size_t extra_size = tape->back_size - old_back_size;
		size_t total_size = tape->back_size + tape->front_size;
		tape->cells = realloc(tape->cells, total_size * sizeof *tape->cells);
		memmove(tape->cells + extra_size, tape->cells, (tape->front_size + old_back_size) * sizeof *tape->cells);
		memset(tape->cells, 0, extra_size * sizeof *tape->cells);

		tape->pos += extra_size;
#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
		tape->bound_upper += extra_size;
		// tape->bound_lower += extra_size; // XXX assuming checking left means bound_lower will be overwritten
#endif
	} else if (pos >= (ssize_t)(tape->back_size + tape->front_size)) {
		size_t old_front_size = tape->front_size;

		size_t total_size = tape->front_size + tape->back_size;
		while ((size_t)pos >= total_size) {
			total_size += tape->front_size;
			tape->front_size *= 2;
		}

		size_t extra_size = tape->front_size - old_front_size;
		tape->cells = realloc(tape->cells, total_size * sizeof *tape->cells);
		memset(tape->cells + old_front_size + tape->back_size, 0, extra_size * sizeof *tape->cells);
	}
}
#endif

void execute_bf(char *restrict what) {
	tape_struct tape = {
		.pos = 16,
#ifndef FIXED_TAPE_SIZE
		.back_size = 16,
		.front_size = 16,
#ifndef NDEBUG
		.bound_upper = 16,
		.bound_lower = 16,
#endif
#endif
	};
#ifdef FIXED_TAPE_SIZE
	tape.cells = calloc(sizeof *tape.cells, (size_t)256 << sizeof(FIXED_TAPE_SIZE));
#else
	tape.cells = calloc(sizeof *tape.cells, tape.front_size + tape.back_size);
#endif

	while (true) {
#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
		if (tape.pos < tape.bound_lower || tape.pos > tape.bound_upper)
			errx(2, "Bounds check failure. Pos: %zu, expected <%zu - %zu>\n", tape.pos, tape.bound_lower, tape.bound_upper);
#endif
		switch (*what++) {
			case BF_OP_ALTER: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				tape.pos += offset;
				tape.cells[tape.pos] += amount;
				break;
			}

#ifndef FIXED_TAPE_SIZE
			case BF_OP_BOUNDS_CHECK: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				tape_ensure_space(&tape, tape.pos + offset);

#ifndef NDEBUG
				if (offset < 0) {
					tape.bound_lower = tape.pos + offset;
				} else {
					tape.bound_upper = tape.pos + offset;
				}
#endif

				break;
			}
#endif

			case BF_OP_ALTER_MOVEONLY: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				tape.pos += offset;
				break;
			}

			case BF_OP_ALTER_ADDONLY: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				tape.cells[tape.pos] += amount;
				break;
			}

			case BF_OP_MULTIPLY: {
				uint8_t repeat = *(uint8_t*)what;
				what++;
				do {
					ssize_t offset = *(ssize_t*)what;
					what += sizeof(ssize_t);
					cell_int amount = *(cell_int*)what;
					what += sizeof(cell_int);

					cell_int orig = tape.cells[tape.pos];
					if (orig == 0) {
						what += (sizeof(ssize_t) + sizeof(cell_int)) * repeat;
						break;
					}
					tape.pos += offset;
					tape.cells[tape.pos] += orig * amount;
					tape.pos -= offset;
				} while (repeat--);

				// Fallthrough to set
			}

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				tape.cells[tape.pos] = amount;
				break;
			}

			case BF_OP_IN: {
				int input = getchar();
				if (input == EOF && sizeof(cell_int) == 1) input = 0;
				tape.cells[tape.pos] = input;
				break;
			}

			case BF_OP_OUT:
				putchar(tape.cells[tape.pos]);
				break;


			case BF_OP_SKIP: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				while (tape.cells[tape.pos] != 0) {
					tape.pos += offset;
#ifndef FIXED_TAPE_SIZE
					tape_ensure_space(&tape, tape.pos);
#endif
				}

#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
				if (offset < 0)
					tape.bound_lower = tape.pos;
				else
					tape.bound_upper = tape.pos;
#endif
				break;
			}

			case BF_OP_JUMPIFZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				if (tape.cells[tape.pos] == 0) {
					what += offset;
				}
				break;
			}

			case BF_OP_JUMPIFNONZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				if (tape.cells[tape.pos] != 0) {
					what += offset;
				}
				break;
			}

			case BF_OP_DIE:
				return;

			default:
				errx(1, "Invalid internal state");
		}
	}
}
