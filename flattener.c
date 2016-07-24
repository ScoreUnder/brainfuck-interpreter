#include <assert.h>
#include "flattener.h"

void blob_ensure_extra(blob_cursor *out, size_t extra) {
	if (out->len < out->pos + extra) {
		out->len *= 2;
		out->data = realloc(out->data, out->len);
	}
}

void flatten_bf(bf_op *op, blob_cursor *out) {
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

		case BF_OP_LOOP:
			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			size_t loop_start = out->pos;
			out->data[out->pos++] = BF_OP_JUMPIFZERO;
			out->pos += sizeof(ssize_t);

			for (size_t i = 0; i < op->children.len; i++)
				flatten_bf(&op->children.ops[i], out);

			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			out->data[out->pos++] = BF_OP_JUMP;
			out->pos += sizeof(ssize_t);

			// Difference between end of first jump instruction and here
			*(ssize_t*)&out->data[loop_start + 1] = out->pos - loop_start - 1 - sizeof(ssize_t);
			// Difference between here and start of first jump instruction
			*(ssize_t*)&out->data[out->pos - sizeof(ssize_t)] = loop_start - out->pos;
			break;

		case BF_OP_ONCE:
			for (size_t i = 0; i < op->children.len; i++)
				flatten_bf(&op->children.ops[i], out);
			blob_ensure_extra(out, 1);
			out->data[out->pos++] = BF_OP_DIE;
			break;

		case BF_OP_SET:
			blob_ensure_extra(out, sizeof(cell_int) + 1);
			out->data[out->pos++] = BF_OP_SET;
			*(cell_int*)&out->data[out->pos] = op->amount;
			out->pos += sizeof(cell_int);
			break;

		case BF_OP_MULTIPLY:
			blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int) + 1);
			out->data[out->pos++] = BF_OP_MULTIPLY;

			*(ssize_t*)&out->data[out->pos] = op->offset;
			out->pos += sizeof(ssize_t);
			*(cell_int*)&out->data[out->pos] = op->amount;
			out->pos += sizeof(cell_int);
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
}
