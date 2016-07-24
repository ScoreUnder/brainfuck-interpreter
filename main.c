#include<stdbool.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<err.h>

#include "parser.h"
#include "flattener.h"
#include "optimizer.h"

struct {
	size_t size;
	size_t back_size;
	ssize_t pos;
	cell_int *cells;
	cell_int *back_cells;
} tape;

void tape_ensure_space(ssize_t pos) {
	if (pos >= 0) {
		if (tape.size <= (size_t) pos) {
			size_t old_size = tape.size;
			while (tape.size <= (size_t) pos) {
				tape.size *= 2;
			}
			tape.cells = realloc(tape.cells, tape.size * sizeof *tape.cells);
			memset(tape.cells + old_size, 0, tape.size - old_size);
		}
	} else {
		pos = -pos;
		if (tape.back_size < (size_t)pos) {
			size_t old_size = tape.back_size;
			while (tape.back_size < (size_t)pos) {
				tape.back_size *= 2;
			}
			tape.back_cells = realloc(tape.back_cells, tape.back_size * sizeof *tape.back_cells);
			memset(tape.back_cells + old_size, 0, tape.back_size - old_size);
		}
	}
}

void execute(char *what) {
#define CELL *(tape.pos >= 0 ? &tape.cells[tape.pos] : &tape.back_cells[-1 - tape.pos])
	while (true) {
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

			case BF_OP_BOUNDS_CHECK: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);

				tape_ensure_space(tape.pos + offset);
				break;
			}

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

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				CELL = amount;
				break;
			}

			case BF_OP_MULTIPLY: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				cell_int amount = *(cell_int*)what;
				what += sizeof(cell_int);

				cell_int orig = CELL;
				if (orig == 0) break;
				tape.pos += offset;
				CELL += orig * amount;
				tape.pos -= offset;
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
					tape_ensure_space(tape.pos);
				}
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

			case BF_OP_JUMP: {
				ssize_t offset = *(ssize_t*)what;
				what += sizeof(ssize_t);
				what += offset;
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

#ifndef NDEBUG
void print_bf_op(bf_op *op, int indent) {
	switch (op->op_type) {
		case BF_OP_ONCE:
			for (size_t i = 0; i < op->children.len; i++)
				print_bf_op(op->children.ops + i, indent);
			break;

		case BF_OP_BOUNDS_CHECK:
			printf("BOUND[%d] ", (int)op->offset);
			break;

		case BF_OP_ALTER:
			if (op->offset > 0)
				printf(">%d", (int)op->offset);
			else if (op->offset < 0)
				printf("<%d", (int)-op->offset);

			if (op->amount && op->offset)
				putchar('_');

			if (op->amount)
				printf("%+d", (int)op->amount);

			putchar(' ');
			break;

		case BF_OP_SET:
			printf("SET%d ", (int)op->amount);
			break;

		case BF_OP_MULTIPLY:
			printf("*%d_@%d ", (int)op->amount, (int)op->offset);
			break;

		case BF_OP_IN:
			printf(", ");
			break;

		case BF_OP_OUT:
			printf(". ");
			break;

		case BF_OP_LOOP:
			printf("[\n%*s", indent += 2, "");
			uint64_t selftime = op->time;
			for (size_t i = 0; i < op->children.len; i++) {
				print_bf_op(op->children.ops + i, indent);
				if (op->children.ops[i].op_type == BF_OP_LOOP)
					selftime -= op->children.ops[i].time;
			}
			indent -= 2;
			if (op->time || op->count)
				printf("\n%*s] (%u @ %lu - %.2lf per, %.2lf self per)\n%*s", indent, "", op->count, op->time, (double)op->time / op->count, (double)selftime / op->count, indent, "");
			else
				printf("\n%*s]\n%*s", indent, "", indent, "");
			break;

		case BF_OP_SKIP:
			printf("S%d ", (int)op->offset);
			break;

		default:
			errx(1, "Invalid internal state");
	}
}
#endif

void free_bf(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_ONCE: {
			size_t children = op->children.len;
			for (size_t i = 0; i < children; i++)
				free_bf(&op->children.ops[i]);
			if (children != 0)
				free(op->children.ops);
			break;
		}

		default:
			// Everything else is fine
			break;
	}
}

/*
 * + (*data)++
 * - (*data)--
 * > data++
 * < data--
 * . out data
 * , in data
 * [ jump to matching ] if !*data
 * ] jump to matching [
 */


int main(int argc, char **argv){
	if (argc != 2) errx(1, "Need exactly 1 argument (file path)");
	char *filename = argv[1];

	int fd = open(filename, O_RDONLY);
	if (fd == -1) err(1, "Can't open file %s", filename);
	size_t size = (size_t) lseek(fd, 0, SEEK_END);
	if (size == (size_t)-1) err(1, "Can't seek file %s", filename);
	char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (map == NULL) err(1, "Can't mmap file %s", filename);

	tape.pos = 0;
	tape.size = 16;
	tape.cells = calloc(sizeof *tape.cells, tape.size);
	tape.back_size = 16;
	tape.back_cells = calloc(sizeof *tape.back_cells, tape.back_size);

	bf_op root = {.op_type = BF_OP_ONCE};
	root.children.ops = build_bf_tree(&(blob_cursor){.data = map, .len = size}, false, &root.children.len);

	bf_op_builder builder = {.out = root.children, .alloc = root.children.len};
	add_bounds_checks(&builder);
	root.children = builder.out;

#ifndef NDEBUG
	print_bf_op(&root, 0);
	printf("\n\n");
#endif

	blob_cursor flat = {
		.data = malloc(128),
		.pos = 0,
		.len = 128,
	};
	flatten_bf(&root, &flat);

	// For the tiny savings this will give us...
	free_bf(&root);

	execute(flat.data);

	free(flat.data);
	munmap(map, size);
	close(fd);
}
