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
	size_t size;
	size_t back_size;
	ssize_t pos;
	cell_int *restrict cells;
	cell_int *restrict back_cells;
} tape_struct;
#endif

#ifndef FIXED_TAPE_SIZE
static void tape_ensure_space(tape_struct *tape, ssize_t pos) {
	if (pos >= 0) {
		if (tape->size <= (size_t) pos) {
			size_t old_size = tape->size;
			while (tape->size <= (size_t) pos) {
				tape->size *= 2;
			}
			tape->cells = realloc(tape->cells, tape->size * sizeof *tape->cells);
			memset(tape->cells + old_size, 0, tape->size - old_size);
		}
	} else {
		pos = -pos;
		if (tape->back_size < (size_t)pos) {
			size_t old_size = tape->back_size;
			while (tape->back_size < (size_t)pos) {
				tape->back_size *= 2;
			}
			tape->back_cells = realloc(tape->back_cells, tape->back_size * sizeof *tape->back_cells);
			memset(tape->back_cells + old_size, 0, tape->back_size - old_size);
		}
	}
}
#endif

void execute_bf(char *restrict what) {
#ifdef FIXED_TAPE_SIZE
#define CELL tape.cells[tape.pos]
#else
#define CELL *(tape.pos >= 0 ? &tape.cells[tape.pos] : &tape.back_cells[-1 - tape.pos])
#endif

#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
	ssize_t bound_upper = 0, bound_lower = 0;
#endif

	tape_struct tape = {
		.pos = 0,
#ifndef FIXED_TAPE_SIZE
		.size = 16,
		.back_size = 16,
#endif
	};
#ifdef FIXED_TAPE_SIZE
	tape.cells = calloc(sizeof *tape.cells, (size_t)256 << sizeof(FIXED_TAPE_SIZE));
#else
	tape.cells = calloc(sizeof *tape.cells, tape.size);
	tape.back_cells = calloc(sizeof *tape.back_cells, tape.back_size);
#endif

	while (true) {
#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
		if (tape.pos < bound_lower || tape.pos > bound_upper)
			errx(2, "Bounds check failure. Pos: %zd, expected <%zd - %zd>\n", tape.pos, bound_lower, bound_upper);
#endif
		switch (*what++) {
			case BF_OP_ALTER: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				tape.pos += offset;
				CELL += amount;
				break;
			}

#ifndef FIXED_TAPE_SIZE
			case BF_OP_BOUNDS_CHECK: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

#ifndef NDEBUG
				if (offset < 0) {
					bound_lower = tape.pos + offset;
				} else {
					bound_upper = tape.pos + offset;
				}
#endif

				tape_ensure_space(&tape, tape.pos + offset);
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

				CELL += amount;
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

					cell_int orig = CELL;
					if (orig == 0) {
						what += (sizeof(ssize_t) + sizeof(cell_int)) * repeat;
						break;
					}
					tape.pos += offset;
					CELL += orig * amount;
					tape.pos -= offset;
				} while (repeat--);

				// Fallthrough to set
			}

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				CELL = amount;
				break;
			}

			case BF_OP_IN: {
				int input = getchar();
				if (input == EOF && sizeof(cell_int) == 1) input = 0;
				CELL = input;
				break;
			}

			case BF_OP_OUT:
				putchar(CELL);
				break;


			case BF_OP_SKIP: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				while (CELL != 0) {
					tape.pos += offset;
#ifndef FIXED_TAPE_SIZE
					tape_ensure_space(&tape, tape.pos);
#endif
				}

#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
				if (offset < 0)
					bound_lower = tape.pos;
				else
					bound_upper = tape.pos;
#endif
				break;
			}

			case BF_OP_JUMPIFZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				if (CELL == 0) {
					what += offset;
				}
				break;
			}

			case BF_OP_JUMPIFNONZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				if (CELL != 0) {
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
#undef CELL
}
