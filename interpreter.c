#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include "assert2.h"
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
inline static void tape_ensure_space(tape_struct *restrict tape, ssize_t pos, interpreter_meta *meta) {
	if (pos + meta->lowest_negative_skip < 0) {
		pos += meta->lowest_negative_skip;  // Always let skips skip without bounds checking
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
	} else if (pos + meta->highest_positive_skip >= (ssize_t)(tape->back_size + tape->front_size)) {
		pos += meta->highest_positive_skip;  // Always let skips skip without bounds checking
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

void execute_bf(char *restrict what, interpreter_meta meta) {
	tape_struct tape = {
#ifndef FIXED_TAPE_SIZE
		.pos = 16 - meta.lowest_negative_skip,
		.back_size = 16 - meta.lowest_negative_skip,
		.front_size = 16 + meta.highest_positive_skip,
#ifndef NDEBUG
		.bound_upper = 16 - meta.lowest_negative_skip,
		.bound_lower = 16 - meta.lowest_negative_skip,
#endif
#else // else if defined FIXED_TAPE_SIZE
		.pos = 16,
#endif
	};
#ifdef FIXED_TAPE_SIZE
	(void)meta;
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

				tape_ensure_space(&tape, tape.pos + offset, &meta);

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
				cell_int orig = tape.cells[tape.pos];
				do {
					ssize_t offset = *(ssize_t*)what;
					what += sizeof(ssize_t);
					cell_int amount = *(cell_int*)what;
					what += sizeof(cell_int);

					if (orig == 0) {
						what += (sizeof(ssize_t) + sizeof(cell_int)) * repeat;
						break;
					}
					tape.cells[tape.pos + offset] += orig * amount;
				} while (repeat--);

				// Fallthrough to set
			}

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				tape.cells[tape.pos] = amount;
				break;
			}

			case BF_OP_SET_MULTI: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				for (ssize_t i = 0; i <= offset; i++)
					tape.cells[tape.pos + i] = amount;
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
				free(tape.cells);
				return;

			default:
				assert(!"Executing an invalid opcode");
		}
	}
}
