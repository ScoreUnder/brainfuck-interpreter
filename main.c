#include<stdbool.h>
#include<stdint.h>
#include<stdlib.h>
#include<stdio.h>
#include<string.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<unistd.h>
#include<err.h>

enum {
	BF_OP_ALTER, BF_OP_IN, BF_OP_OUT, BF_OP_LOOP,
	// Pseudo-ops
	BF_OP_ONCE,
	// Optimized ops
	BF_OP_SET, BF_OP_MULTIPLY, BF_OP_SKIP,
};

typedef int8_t cell_int;

typedef struct s_bf_op {
	unsigned int op_type;
	unsigned int count;
	uint64_t time;
	union {
		struct {
			size_t child_op_count;
			struct s_bf_op *child_op;
		};
		struct {
			ssize_t offset;
			cell_int amount;
		};
	};
} bf_op;

static char op_type_for_char[] = {
	['+'] = BF_OP_ALTER,
	['-'] = BF_OP_ALTER,
	['<'] = BF_OP_ALTER,
	['>'] = BF_OP_ALTER,
	[','] = BF_OP_IN,
	['.'] = BF_OP_OUT,
	['['] = BF_OP_LOOP,
};

bf_op* build_bf_tree(char *data, size_t *pos, size_t len, bool expecting_bracket, size_t *out_len) {
	size_t allocated = 16, my_ops_len = 0;
	bf_op *my_ops = malloc(allocated * sizeof *my_ops);

	size_t i = *pos;
	while (i < len) {
		unsigned char c = data[i++];
		switch (c) {
			case '+':
			case '-':
				if (my_ops_len == 0
						|| (my_ops[my_ops_len - 1].op_type != BF_OP_ALTER
							&& my_ops[my_ops_len - 1].op_type != BF_OP_SET)) {
new_alter:
					if (my_ops_len == allocated) {
						allocated *= 2;
						my_ops = realloc(my_ops, allocated * sizeof *my_ops);
					}
					my_ops[my_ops_len++] = (bf_op){
						.op_type = BF_OP_ALTER,
						.offset = 0,
						.amount = 0,
					};
				}
				break;
			case '>':
			case '<':
				if (my_ops_len == 0
						|| my_ops[my_ops_len - 1].op_type != BF_OP_ALTER
						|| my_ops[my_ops_len - 1].amount != 0) {
					goto new_alter;
				}
				break;
			case '.':
			case ',':
			case '[':
				if (my_ops_len == allocated) {
					allocated *= 2;
					my_ops = realloc(my_ops, allocated * sizeof *my_ops);
				}
				my_ops[my_ops_len++] = (bf_op){
					.op_type = op_type_for_char[c],
				};
				break;
		}

		bf_op *op = &my_ops[my_ops_len - 1];
		switch (c) {
			case '+':
				op->amount++;
				break;
			case '-':
				op->amount--;
				break;
			case '>':
				op->offset++;
				break;
			case '<':
				op->offset--;
				break;
			case '[':
				op->child_op = build_bf_tree(data, &i, len, true, &op->child_op_count);
				// Shite optimizations
				if (op->child_op_count == 1
						&& op->child_op[0].op_type == BF_OP_ALTER
						&& op->child_op[0].offset == 0
						&& (op->child_op[0].amount == 1
							|| op->child_op[0].amount == (cell_int)-1)) {
					free(op->child_op);
					op->op_type = BF_OP_SET;
					op->amount = 0;
				} else if (op->child_op_count == 2
						&& op->child_op[0].op_type == BF_OP_ALTER
						&& op->child_op[1].op_type == BF_OP_ALTER
						&& op->child_op[0].offset == -op->child_op[1].offset
						&& op->child_op[1].amount == (cell_int)-1) {
					ssize_t offset = op->child_op[0].offset;
					cell_int scalar = op->child_op[0].amount;
					free(op->child_op);
					op->op_type = BF_OP_MULTIPLY;
					op->offset = offset;
					op->amount = scalar;
				} else if (op->child_op_count == 3
						&& op->child_op[0].op_type == BF_OP_ALTER
						&& op->child_op[1].op_type == BF_OP_ALTER
						&& op->child_op[2].op_type == BF_OP_ALTER
						&& op->child_op[0].offset == 0
						&& op->child_op[1].offset == -op->child_op[2].offset
						&& op->child_op[0].amount == (cell_int)-1
						&& op->child_op[2].amount == 0) {
					// TODO this should really be handled by some kind of optimizer which shuffles lonely incs/decs to the right
					ssize_t offset = op->child_op[1].offset;
					cell_int scalar = op->child_op[1].amount;
					free(op->child_op);
					op->op_type = BF_OP_MULTIPLY;
					op->offset = offset;
					op->amount = scalar;
				} else if (op->child_op_count == 1
						&& op->child_op[0].op_type == BF_OP_ALTER
						&& op->child_op[0].amount == 0) {
					ssize_t offset = op->child_op[0].offset;
					free(op->child_op);
					op->op_type = BF_OP_SKIP;
					op->offset = offset;
				}
				break;
			case ']':
				if (!expecting_bracket) errx(1, "Unexpected end of loop");
				goto end;
		}
	}

end:
	*pos = i;
	*out_len = my_ops_len;
	if (my_ops_len == 0) {
		free(my_ops);
		my_ops = NULL;
	}
	return my_ops;
}

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

void execute(bf_op *op) {
#define CELL *(tape.pos >= 0 ? &tape.cells[tape.pos] : &tape.back_cells[-1 - tape.pos])
	switch (op->op_type) {
		case BF_OP_ONCE:
			for (size_t i = 0; i < op->child_op_count; i++)
				execute(op->child_op + i);
			break;

		case BF_OP_ALTER:
			tape.pos += op->offset;
			tape_ensure_space(tape.pos);
			CELL += op->amount;
			break;

		case BF_OP_SET:
			CELL = op->amount;
			break;

		case BF_OP_MULTIPLY: {
			cell_int orig = CELL;
			if (orig == 0) break;
			tape.pos += op->offset;
			tape_ensure_space(tape.pos);
			CELL += orig * op->amount;
			tape.pos -= op->offset;
			CELL = 0;
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

		case BF_OP_LOOP: {
			uint32_t hi, lo;
			__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
			uint64_t start = ((uint64_t)hi << 32) | ((uint64_t)lo);
			while (CELL != 0) {
				for (size_t i = 0; i < op->child_op_count; i++)
					execute(op->child_op + i);
				op->count++;
			}
			__asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
			uint64_t end = ((uint64_t)hi << 32) | ((uint64_t)lo);
			op->time += end - start;
			break;
		}

		case BF_OP_SKIP:
			while (CELL != 0) {
				tape.pos += op->offset;
				tape_ensure_space(tape.pos);
			}
			break;

		default:
			errx(1, "Invalid internal state");
	}
#undef CELL
}

void print(bf_op *op, int indent) {
	switch (op->op_type) {
		case BF_OP_ONCE:
			for (size_t i = 0; i < op->child_op_count; i++)
				print(op->child_op + i, indent);
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
			for (size_t i = 0; i < op->child_op_count; i++)
				print(op->child_op + i, indent);
			indent -= 2;
			printf("\n%*s] (%u @ %lu - %lf per)\n%*s", indent, "", op->count, op->time, (double)op->time / op->count, indent, "");
			break;

		case BF_OP_SKIP:
			printf("S%d ", (int)op->offset);
			break;

		default:
			errx(1, "Invalid internal state");
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

	size_t pos = 0;
	bf_op root = {.op_type = BF_OP_ONCE};
	root.child_op = build_bf_tree(map, &pos, size, false, &root.child_op_count);

	print(&root, 0);
	printf("\n\n");
	execute(&root);
	printf("\n\n");
	print(&root, 0);

	munmap(map, size);
	close(fd);
}
