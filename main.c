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
#include "interpreter.h"

#ifndef NDEBUG
void print_bf_op(bf_op *op, int indent) {
	switch (op->op_type) {
		case BF_OP_ONCE:
			for (size_t i = 0; i < op->children.len; i++)
				print_bf_op(op->children.ops + i, indent);
			break;

#ifndef FIXED_TAPE_SIZE
		case BF_OP_BOUNDS_CHECK:
			printf("BOUND[%d] ", (int)op->offset);
			break;
#endif

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
			for (size_t i = 0; i < op->children.len; i++) {
				print_bf_op(op->children.ops + i, indent);
			}
			indent -= 2;
			printf("\n%*s] (uncertainties: ", indent, "");
			if (op->uncertainty & 2) putchar('<');
			if (op->uncertainty & 1) putchar('>');
			printf(")\n%*s", indent, "");
			break;

		case BF_OP_SKIP:
			printf("S%d ", (int)op->offset);
			break;

		default:
			errx(1, "Invalid internal state");
	}
}

void print_flattened(char *restrict opcodes) {
	size_t address = 0;
	while (true) {
		size_t start_address = address;
		switch (opcodes[address++]) {
			case BF_OP_ALTER: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);
				cell_int amount = *(cell_int*)&opcodes[address];
				address += sizeof(cell_int);

				printf("%08zx: ALTER >%zd %+d\n", start_address, offset, (int)amount);
				break;
			}

#ifndef FIXED_TAPE_SIZE
			case BF_OP_BOUNDS_CHECK: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);

				printf("%08zx: BOUNDS_CHECK %zd\n", start_address, offset);
				break;
			}
#endif

			case BF_OP_ALTER_MOVEONLY: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);

				printf("%08zx: ALTER_MOVEONLY >%zd\n", start_address, offset);
				break;
			}

			case BF_OP_ALTER_ADDONLY: {
				cell_int amount = *(cell_int*)&opcodes[address];
				address += sizeof(cell_int);

				printf("%08zx: ALTER_ADDONLY %+d\n", start_address, (int)amount);
				break;
			}

			case BF_OP_MULTIPLY: {
				uint8_t repeat = *(uint8_t*)&opcodes[address];
				address++;
				printf("%08zx: MULTIPLY {\n", start_address);
				do {
					size_t this_address = address;
					ssize_t offset = *(ssize_t*)&opcodes[address];
					address += sizeof(ssize_t);
					cell_int amount = *(cell_int*)&opcodes[address];
					address += sizeof(cell_int);

					printf("%08zx: \t@%zd *%d\n", this_address, offset, (int)amount);
				} while (repeat--);
				printf("%08zx: }\n", address);

				// Fallthrough to set
			}

			case BF_OP_SET: {
				cell_int amount = *(cell_int*)&opcodes[address];
				address += sizeof(cell_int);

				printf("%08zx: SET %d\n", start_address, (int)amount);
				break;
			}

			case BF_OP_IN:
				printf("%08zx: IN\n", start_address);
				break;

			case BF_OP_OUT:
				printf("%08zx: OUT\n", start_address);
				break;


			case BF_OP_SKIP: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);

				printf("%08zx: SKIP >%zd\n", start_address, offset);
				break;
			}

			case BF_OP_JUMPIFZERO: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);
				printf("%08zx: JUMPIFZERO %+zd (%zx)\n", start_address, offset, address + offset);
				break;
			}

			case BF_OP_JUMPIFNONZERO: {
				ssize_t offset = *(ssize_t*)&opcodes[address];
				address += sizeof(ssize_t);
				printf("%08zx: JUMPIFNONZERO %+zd (%zx)\n", start_address, offset, address + offset);
				break;
			}

			case BF_OP_DIE:
				printf("%08zx: DIE\n", start_address);
				return;

			default:
				errx(1, "Invalid internal state");
		}
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
	if (map == MAP_FAILED) err(1, "Can't mmap file %s", filename);

	bf_op root = {.op_type = BF_OP_ONCE};
	root.children.ops = build_bf_tree(&(blob_cursor){.data = map, .len = size}, false, &root.children.len);

#ifndef FIXED_TAPE_SIZE
	bf_op_builder builder = {
		.out = {
			.ops = root.children.ops,
			.len = root.children.len,
		},
		.alloc = root.children.len,
	};
	add_bounds_checks(&builder);
	root.children = builder.out;
#endif

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

#ifndef NDEBUG
	print_flattened(flat.data);
	printf("\n\n");
#endif

	execute_bf(flat.data);

	free(flat.data);
	munmap(map, size);
	close(fd);
}
