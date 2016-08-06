#include <assert.h>
#include "optimizer_helpers.h"

bool ensures_zero(bf_op const *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_SKIP:
			return true;
		case BF_OP_SET:
			return op->amount == 0;
		default:
			return false;
	}
}

bool ensures_nonzero(bf_op const *op) {
	switch (op->op_type) {
		case BF_OP_SET:
			return op->amount != 0;
		default:
			return false;
	}
}

bool writes_cell(bf_op const *op) {
	switch (op->op_type) {
		case BF_OP_ALTER:
			return op->amount != 0;
		case BF_OP_LOOP:
		case BF_OP_MULTIPLY:
		case BF_OP_IN:
		case BF_OP_SET:
			return true;
		case BF_OP_OUT:
		case BF_OP_SKIP:
			return false;
		default:
			assert(!"Unexpected opcode");
			return false;
	}
}

bool moves_tape(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_SKIP:
			return true;
		case BF_OP_ALTER:
			return op->offset != 0;
		case BF_OP_LOOP: {
			loop_info info = get_loop_info(op);
			return info.uncertain_forwards || info.uncertain_backwards;
		}
		case BF_OP_IN:
		case BF_OP_OUT:
		case BF_OP_SET:
		case BF_OP_MULTIPLY:
			return false;
		default:
			assert(!"Unexpected opcode");
			return false;
	}
}

static bool loops_once_at_most(bf_op *loop) {
	assert(loop->op_type == BF_OP_LOOP);

	if (loop->children.len == 0) return false;

	bf_op *last = &loop->children.ops[loop->children.len - 1];
	if (ensures_zero(last))
		return true;
	if (last->definitely_zero && !moves_tape(last) && !writes_cell(last))
		return true;

	return false;
}

loop_info get_loop_info(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.ops != NULL || op->children.len == 0);

	loop_info info = op->info;

	if (info.calculated)
		return info;

	for (size_t i = 0; i < op->children.len; i++) {
		bf_op *child = &op->children.ops[i];
		if (child->op_type == BF_OP_ALTER) {
			if (!info.inner_uncertain_backwards)
				info.offset_lower += child->offset;
			if (!info.inner_uncertain_forwards)
				info.offset_upper += child->offset;
		} else if (child->op_type == BF_OP_SKIP) {
			assert(child->offset != 0);
			if (child->offset > 0) {
				info.inner_uncertain_forwards = true;
			} else {
				info.inner_uncertain_backwards = true;
			}

			if (info.inner_uncertain_forwards && info.inner_uncertain_backwards) break;
		} else if (child->op_type == BF_OP_LOOP) {
			loop_info child_info = get_loop_info(child);
			if (child_info.loops_once_at_most) {
				if (child_info.inner_uncertain_forwards) {
					info.inner_uncertain_forwards = true;
				} else if (info.offset_upper > 0) {
					info.offset_upper += child_info.offset_upper;
				}
				if (child_info.inner_uncertain_backwards) {
					info.inner_uncertain_backwards = true;
				} else if (info.offset_lower < 0) {
					info.offset_lower += child_info.offset_lower;
				}
			} else {
				info.inner_uncertain_forwards  |= child_info.uncertain_forwards;
				info.inner_uncertain_backwards |= child_info.uncertain_backwards;
			}

			if (info.uncertain_forwards && info.uncertain_backwards) break;
		}
	}

	info.loops_once_at_most = loops_once_at_most(op);

	info.uncertain_forwards = info.offset_upper > 0 || info.inner_uncertain_forwards;
	info.uncertain_backwards = info.offset_lower < 0 || info.inner_uncertain_backwards;
	info.calculated = true;
	op->info = info;
	return info;
}
