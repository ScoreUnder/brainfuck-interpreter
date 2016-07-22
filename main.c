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
	BF_OP_ALTER, BF_OP_IN, BF_OP_OUT, BF_OP_LOOP, BF_OP_ONCE
};

typedef struct s_bf_op {
	unsigned int op_type;
	union {
		struct {
			size_t child_op_count;
			struct s_bf_op *child_op;
		};
		struct {
			int offset;
			int amount;
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
						|| my_ops[my_ops_len - 1].op_type != BF_OP_ALTER) {
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
	uint8_t *cells;
	uint8_t *back_cells;
} tape;

void execute(bf_op *op) {
#define CELL *(tape.pos >= 0 ? &tape.cells[tape.pos] : &tape.back_cells[-1 - tape.pos])
	switch (op->op_type) {
		case BF_OP_ONCE:
			for (size_t i = 0; i < op->child_op_count; i++)
				execute(op->child_op + i);
			break;

		case BF_OP_ALTER:
			tape.pos += op->offset;

			if (tape.pos >= 0) {
				if (tape.size <= (size_t) tape.pos) {
					size_t old_size = tape.size;
					while (tape.size <= (size_t) tape.pos) {
						tape.size *= 2;
					}
					tape.cells = realloc(tape.cells, tape.size * sizeof *tape.cells);
					memset(tape.cells + old_size, 0, tape.size - old_size);
				}
			} else {
				if (tape.back_size < (size_t)-tape.pos) {
					size_t old_size = tape.back_size;
					while (tape.back_size < (size_t)-tape.pos) {
						tape.size *= 2;
					}
					tape.back_cells = realloc(tape.back_cells, tape.back_size * sizeof *tape.back_cells);
					memset(tape.back_cells + old_size, 0, tape.back_size - old_size);
				}
			}

			CELL += op->amount;
			break;

		case BF_OP_IN:
			CELL = getchar();
			break;

		case BF_OP_OUT:
			putchar(CELL);
			break;

		case BF_OP_LOOP:
			while (CELL != 0)
				for (size_t i = 0; i < op->child_op_count; i++)
					execute(op->child_op + i);
			break;

		default:
			errx(1, "Invalid internal state");
	}
#undef CELL
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

	execute(&root);

	munmap(map, size);
	close(fd);
}
