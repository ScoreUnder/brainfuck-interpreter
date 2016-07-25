#include <assert.h>
#include "flattener.h"

static void blob_ensure_extra(blob_cursor *out, size_t extra) {
	if (out->len < out->pos + extra) {
		out->len *= 2;
		out->data = realloc(out->data, out->len);
	}
}

static ssize_t flatten_bf_internal(bf_op *op, blob_cursor *out, ssize_t previous_op) {
	ssize_t op_start = (ssize_t)out->pos;
	switch (op->op_type) {
		case BF_OP_ALTER:
			if (op->offset && op->amount) {
				blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_ALTER;
			} else if (op->offset) {
				blob_ensure_extra(out, sizeof(ssize_t) + 1);
				out->data[out->pos++] = BF_OP_ALTER_MOVEONLY;
			} else {
				assert(op->amount);
				blob_ensure_extra(out, sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_ALTER_ADDONLY;
			}

			if (op->offset) {
				*(ssize_t*)&out->data[out->pos] = op->offset;
				out->pos += sizeof(ssize_t);
			}
			if (op->amount) {
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
			}
			break;

		case BF_OP_LOOP: {
			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			size_t loop_start = out->pos;
			out->data[out->pos++] = BF_OP_JUMPIFZERO;
			out->pos += sizeof(ssize_t);

			ssize_t previous_op = -1;
			for (size_t i = 0; i < op->children.len; i++)
				previous_op = flatten_bf_internal(&op->children.ops[i], out, previous_op);

			if (previous_op == -1 || out->data[previous_op] != BF_OP_JUMPIFNONZERO) {
				blob_ensure_extra(out, sizeof(ssize_t) + 1);
				op_start = out->pos; // Some things might want to merge with our JNZ
				out->data[out->pos++] = BF_OP_JUMPIFNONZERO;
				out->pos += sizeof(ssize_t);
			} else {
				// Merge with the previous JNZ
				op_start = previous_op;
			}

			// Difference between end of first jump instruction and here
			ssize_t jump_distance = out->pos - loop_start - 1 - sizeof(ssize_t);
			*(ssize_t*)&out->data[loop_start + 1] = jump_distance;

			if (previous_op == -1 || out->data[previous_op] != BF_OP_JUMPIFNONZERO) {
				// On the nonzero jump, skip all jump-if-zeros because they will never fire
				while (out->data[out->pos - jump_distance] == BF_OP_JUMPIFZERO) {
					jump_distance -= sizeof(ssize_t) + 1;
				}
				*(ssize_t*)&out->data[out->pos - sizeof(ssize_t)] = -jump_distance;
			}
			break;
		}

		case BF_OP_ONCE: {
			ssize_t previous_op = -1;
			for (size_t i = 0; i < op->children.len; i++)
				previous_op = flatten_bf_internal(&op->children.ops[i], out, previous_op);
			blob_ensure_extra(out, 1);
			out->data[out->pos++] = BF_OP_DIE;
			break;
		}

		case BF_OP_SET:
			if (previous_op != -1 && out->data[previous_op] == BF_OP_MULTIPLY) {
				blob_ensure_extra(out, sizeof(cell_int));
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
				return -1;
			} else {
				blob_ensure_extra(out, sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_SET;
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
			}
			break;

		case BF_OP_MULTIPLY:
			if (previous_op != -1 && out->data[previous_op] == BF_OP_MULTIPLY
					&& (uint8_t)out->data[previous_op + 1] != 0xFF) {
				blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int));
				out->data[previous_op + 1]++;

				*(ssize_t*)&out->data[out->pos] = op->offset;
				out->pos += sizeof(ssize_t);
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
				return previous_op;
			} else {
				blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_MULTIPLY;
				out->data[out->pos++] = 0;

				*(ssize_t*)&out->data[out->pos] = op->offset;
				out->pos += sizeof(ssize_t);
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
			}
			break;

		case BF_OP_BOUNDS_CHECK:
		case BF_OP_SKIP:
			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			out->data[out->pos++] = op->op_type;
			*(ssize_t*)&out->data[out->pos] = op->offset;
			out->pos += sizeof(ssize_t);
			break;

		default:
			blob_ensure_extra(out, 1);
			out->data[out->pos++] = op->op_type;
			break;
	}
	return op_start;
}

void flatten_bf(bf_op *op, blob_cursor *out) {
	flatten_bf_internal(op, out, -1);
}
