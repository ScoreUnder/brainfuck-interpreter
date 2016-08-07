#include <stdio.h>

#include "assert2.h"
#include "interpreter.h"
#include "brainfuck.h"

#define CPP_STRINGIFY2(x) #x
#define CPP_STRINGIFY(x) CPP_STRINGIFY2(x)
#ifdef FIXED_TAPE_SIZE
typedef struct {
	FIXED_TAPE_SIZE pos;
	cell_int *restrict cells;
} tape_struct;
#else
typedef struct {
	size_t back_size;
	size_t front_size;
	size_t pos;
	cell_int *restrict cells;
#ifndef NDEBUG
	size_t bound_upper, bound_lower;
#endif
} tape_struct;
#endif

void execute_bf(char *restrict what, interpreter_meta meta) {
	puts("#include <stdio.h>\n"
	     "#include <stdlib.h>\n"
	     "#include <stdint.h>");
#ifndef NDEBUG
	puts("#include <err.h>");
#endif
#ifndef FIXED_TAPE_SIZE
	puts("#include <string.h>");
#endif
	puts("");

#ifdef FIXED_TAPE_SIZE
	(void)meta;
	puts("typedef struct {\n"
	     "	" CPP_STRINGIFY(FIXED_TAPE_SIZE) " pos;\n"
	     "	" CPP_STRINGIFY(CELL_INT) " *restrict cells;\n"
	     "} tape_struct;\n");
#else
	// Constants used for tape size
	printf("#define LOWEST_NEGATIVE_SKIP %zd\n"
	       "#define HIGHEST_POSITIVE_SKIP %zd\n"
	       "#define TAPE_INITIAL 16\n",
	       meta.lowest_negative_skip, meta.highest_positive_skip);

	// Tape struct
	puts("typedef struct {\n"
	     "	size_t back_size;\n"
	     "	size_t front_size;\n"
	     "	size_t pos;\n"
	     "	" CPP_STRINGIFY(CELL_INT) " *restrict cells;\n"
#ifndef NDEBUG
	     "	size_t bound_upper, bound_lower;\n"
#endif
	     "} tape_struct;\n");

	// Tape reallocator
	puts("static void tape_ensure_space(tape_struct *restrict tape, ssize_t pos) {\n"
	     "	if (pos + LOWEST_NEGATIVE_SKIP < 0) {\n"
	     "		pos += LOWEST_NEGATIVE_SKIP;  // Always let skips skip without bounds checking\n"
	     "		size_t old_back_size = tape->back_size;\n"
	     "		while (pos < 0) {\n"
	     "			pos += tape->back_size;\n"
	     "			tape->back_size *= 2;\n"
	     "		}\n"
	     "		size_t extra_size = tape->back_size - old_back_size;\n"
	     "		size_t total_size = tape->back_size + tape->front_size;\n"
	     "		tape->cells = realloc(tape->cells, total_size * sizeof *tape->cells);\n"
	     "		memmove(tape->cells + extra_size, tape->cells, (tape->front_size + old_back_size) * sizeof *tape->cells);\n"
	     "		memset(tape->cells, 0, extra_size * sizeof *tape->cells);\n"
	     "\n"
	     "		tape->pos += extra_size;\n"
#ifndef NDEBUG
	     "		tape->bound_upper += extra_size;\n"
#endif
	     "	} else if (pos + HIGHEST_POSITIVE_SKIP >= (ssize_t)(tape->back_size + tape->front_size)) {\n"
	     "		pos += HIGHEST_POSITIVE_SKIP;  // Always let skips skip without bounds checking\n"
	     "		size_t old_front_size = tape->front_size;\n"
	     "\n"
	     "		size_t total_size = tape->front_size + tape->back_size;\n"
	     "		while ((size_t)pos >= total_size) {\n"
	     "			total_size += tape->front_size;\n"
	     "			tape->front_size *= 2;\n"
	     "		}\n"
	     "\n"
	     "		size_t extra_size = tape->front_size - old_front_size;\n"
	     "		tape->cells = realloc(tape->cells, total_size * sizeof *tape->cells);\n"
	     "		memset(tape->cells + old_front_size + tape->back_size, 0, extra_size * sizeof *tape->cells);\n"
	     "	}\n"
	     "}\n"
#ifndef NDEBUG
	     "void bounds_check(tape_struct *restrict tape) {\n"
	     "	if (tape->pos < tape->bound_lower || tape->pos > tape->bound_upper)\n"
	     "		errx(2, \"Bounds check failure. Pos: %zu, expected <%zu - %zu>\\n\", tape->pos, tape->bound_lower, tape->bound_upper);\n"
	     "}\n"
#endif
	);
#endif

	// Main function
	puts("int main(){\n"
	     "	tape_struct tape = {\n"
#ifndef FIXED_TAPE_SIZE
	     "		.pos = TAPE_INITIAL - LOWEST_NEGATIVE_SKIP,\n"
	     "		.back_size = TAPE_INITIAL - LOWEST_NEGATIVE_SKIP,\n"
	     "		.front_size = TAPE_INITIAL + HIGHEST_POSITIVE_SKIP,\n"
#ifndef NDEBUG
	     "		.bound_upper = TAPE_INITIAL - LOWEST_NEGATIVE_SKIP,\n"
	     "		.bound_lower = TAPE_INITIAL - LOWEST_NEGATIVE_SKIP,\n"
#endif
#else // else if defined FIXED_TAPE_SIZE
	     "		.pos = 16,\n"
#endif
	     "	};\n"

#ifdef FIXED_TAPE_SIZE
	     "	tape.cells = calloc(sizeof *tape.cells, (size_t)256 << sizeof(" CPP_STRINGIFY(FIXED_TAPE_SIZE) "));\n"
#else
	     "	tape.cells = calloc(sizeof *tape.cells, tape.front_size + tape.back_size);\n"
#endif
	);

	char *ops_orig = what;
	while (true) {
		printf("instr_%08zx:\n", what - ops_orig);
#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
		puts("	bounds_check(&tape);\n");
#endif
		switch (*what++) {
			case BF_OP_ALTER: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				printf("	tape.cells[tape.pos += %zd] += %d;\n", offset, (int)amount);
				break;
			}

#ifndef FIXED_TAPE_SIZE
			case BF_OP_BOUNDS_CHECK: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				printf("	tape_ensure_space(&tape, tape.pos + %zd);\n", offset);

#ifndef NDEBUG
				if (offset < 0) {
					printf("	tape.bound_lower = tape.pos + %zd;\n", offset);
				} else {
					printf("	tape.bound_upper = tape.pos + %zd;\n", offset);
				}
#endif

				break;
			}
#endif

			case BF_OP_ALTER_MOVEONLY: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				printf("	tape.pos += %zd;\n", offset);
				break;
			}

			case BF_OP_ALTER_ADDONLY: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				printf("	tape.cells[tape.pos] += %d;\n", (int)amount);
				break;
			}

			case BF_OP_MULTIPLY: {
				uint8_t repeat = *(uint8_t*)what;
				what++;
				puts("	{\n"
				     "		" CPP_STRINGIFY(CELL_INT) " val = tape.cells[tape.pos];");
				do {
					ssize_t offset = *(ssize_t*)what;
					what += sizeof(ssize_t);
					cell_int amount = *(cell_int*)what;
					what += sizeof(cell_int);

					if (amount == 1) {
						printf("		tape.cells[tape.pos + %zd] += val;\n", offset);
					} else if (amount == -1) {
						printf("		tape.cells[tape.pos + %zd] -= val;\n", offset);
					} else {
						printf("		tape.cells[tape.pos + %zd] += val * %d;\n", offset, (int)amount);
					}
				} while (repeat--);
				puts("	}");

				// Fallthrough to set
			}

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				printf("	tape.cells[tape.pos] = %d;\n", (int)amount);
				break;
			}

			case BF_OP_SET_MULTI: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				printf("	for (size_t i = 0; i <= %zd; i++)\n"
				       "		tape.cells[tape.pos + i] = %d;\n",
					offset, (int)amount);
				break;
			}

			case BF_OP_IN:
				if (sizeof(cell_int) == 1) {
					puts("	{\n"
					     "		int input = getchar();\n"
					     "		if (input == EOF) input = 0;\n"
					     "		tape.cells[tape.pos] = input;\n"
					     "	}");
				} else {
					puts("	{\n"
					     "		int input = getchar();\n"
					     "		tape.cells[tape.pos] = input;\n"
					     "	}");
				}
				break;

			case BF_OP_OUT:
				puts("	putchar(tape.cells[tape.pos]);");
				break;


			case BF_OP_SKIP: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				printf("	while (tape.cells[tape.pos] != 0) {\n"
				       "		tape.pos += %zd;\n"
				       "	}\n",
				       offset);

#if !defined(NDEBUG) && !defined(FIXED_TAPE_SIZE)
				if (offset < 0)
					printf("	tape.bound_lower = tape.pos;\n");
				else
					printf("	tape.bound_upper = tape.pos;\n");
#endif
				break;
			}

			case BF_OP_JUMPIFZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				printf("	if (!tape.cells[tape.pos]) goto instr_%08zx;\n", what + offset - ops_orig);
				break;
			}

			case BF_OP_JUMPIFNONZERO: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				printf("	if (tape.cells[tape.pos]) goto instr_%08zx;\n", what + offset - ops_orig);
				break;
			}

			case BF_OP_DIE:
				puts("	return 0;\n"
				     "}");
				return;

			default:
				assert(!"Compiling an invalid opcode");
		}
	}
}
