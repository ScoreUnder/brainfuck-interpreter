#include <err.h>
#include <stdbool.h>
#include <stdio.h>

#include "debug.h"

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
