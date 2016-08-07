#include <assert.h>
#include <stdbool.h>

#include "optimizer_helpers.h"
#include "flattener.h"

static void blob_ensure_extra(blob_cursor *out, size_t extra) {
	if (out->len < out->pos + extra) {
		out->len *= 2;
		out->data = realloc(out->data, out->len);
	}
}

typedef struct {
	interpreter_meta interp_meta;
	ssize_t previous_op;
} flattener_state;

static void flatten_bf_internal(bf_op *op, blob_cursor *out, flattener_state *state) {
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
			size_t loop_start = out->pos;
			bool have_initial_jump = !op->definitely_nonzero;
			if (have_initial_jump) {
				blob_ensure_extra(out, sizeof(ssize_t) + 1);
				out->data[out->pos++] = BF_OP_JUMPIFZERO;
				out->pos += sizeof(ssize_t);
			}
			size_t loop_body_start = out->pos;

			state->previous_op = -1;
			for (size_t i = 0; i < op->children.len; i++)
				flatten_bf_internal(&op->children.ops[i], out, state);

			bool have_final_jump = !get_loop_info(op).loops_once_at_most;
			if (have_final_jump) {
				blob_ensure_extra(out, sizeof(ssize_t) + 1);
				out->data[out->pos++] = BF_OP_JUMPIFNONZERO;
				out->pos += sizeof(ssize_t);
			}

			// Difference between end of first jump instruction and here
			ssize_t jump_distance = out->pos - loop_body_start;
			if (have_initial_jump)
				*(ssize_t*)&out->data[loop_start + 1] = jump_distance;

			if (have_final_jump) {
				// On the nonzero jump, skip all jump-if-zeros because they will never fire
				while (out->data[out->pos - jump_distance] == BF_OP_JUMPIFZERO) {
					jump_distance -= sizeof(ssize_t) + 1;
				}
				*(ssize_t*)&out->data[out->pos - sizeof(ssize_t)] = -jump_distance;
			}
			// Can't merge with loops (or "if"s)
			op_start = -1;
			break;
		}

		case BF_OP_ONCE: {
			state->previous_op = -1;
			for (size_t i = 0; i < op->children.len; i++)
				flatten_bf_internal(&op->children.ops[i], out, state);
			op_start = -1;
			blob_ensure_extra(out, 1);
			out->data[out->pos++] = BF_OP_DIE;
			break;
		}

		case BF_OP_SET: {
			bool was_multiply = state->previous_op != -1 && out->data[state->previous_op] == BF_OP_MULTIPLY;
			bool is_multi = op->offset != 0;
			if (was_multiply) {
				blob_ensure_extra(out, sizeof(cell_int));
				if (is_multi) {
					// Multi-set after multiply = can just throw away a cell_int anyway...
					*(cell_int*)&out->data[out->pos] = op->amount;
					out->pos += sizeof(cell_int);
					// ...and from now on it's a normal multi-set
					op_start = out->pos;
				} else {
					op_start = -1;  // Final SET after a multiply sequence can't merge with anything
				}
			}
			if (is_multi) {
				blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_SET_MULTI;
				*(ssize_t*)&out->data[out->pos] = op->offset;
				out->pos += sizeof(ssize_t);
			} else if (!was_multiply) {
				blob_ensure_extra(out, sizeof(cell_int) + 1);
				out->data[out->pos++] = BF_OP_SET;
			}
			*(cell_int*)&out->data[out->pos] = op->amount;
			out->pos += sizeof(cell_int);
			break;
		}

		case BF_OP_MULTIPLY:
			if (state->previous_op != -1 && out->data[state->previous_op] == BF_OP_MULTIPLY
					&& (uint8_t)out->data[state->previous_op + 1] != 0xFF) {
				blob_ensure_extra(out, sizeof(ssize_t) + sizeof(cell_int));
				out->data[state->previous_op + 1]++;

				*(ssize_t*)&out->data[out->pos] = op->offset;
				out->pos += sizeof(ssize_t);
				*(cell_int*)&out->data[out->pos] = op->amount;
				out->pos += sizeof(cell_int);
				op_start = state->previous_op;
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
			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			out->data[out->pos++] = op->op_type;
			*(ssize_t*)&out->data[out->pos] = op->offset;
			out->pos += sizeof(ssize_t);
			break;

		case BF_OP_SKIP:
			blob_ensure_extra(out, sizeof(ssize_t) + 1);
			out->data[out->pos++] = op->op_type;
			*(ssize_t*)&out->data[out->pos] = op->offset;
			out->pos += sizeof(ssize_t);

			if (op->offset < state->interp_meta.lowest_negative_skip) {
				state->interp_meta.lowest_negative_skip = op->offset;
			} else if (op->offset > state->interp_meta.highest_positive_skip) {
				state->interp_meta.highest_positive_skip = op->offset;
			}

			break;

		default:
			blob_ensure_extra(out, 1);
			out->data[out->pos++] = op->op_type;
			break;
	}
	state->previous_op = op_start;
}

interpreter_meta flatten_bf(bf_op *op, blob_cursor *out) {
	flattener_state state = {.previous_op = -1};
	flatten_bf_internal(op, out, &state);
	return state.interp_meta;
}
