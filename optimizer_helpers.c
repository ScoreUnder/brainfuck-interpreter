#include "assert2.h"
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

bool expects_nonzero(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_SKIP:
		case BF_OP_MULTIPLY:
			return true;
		case BF_OP_SET:
			// If it's already 0, this SET wanted to change something from
			// nonzero.
			// TODO: op->offset doesn't matter if we know all cells are zero
			return op->amount == 0 && op->offset == 0;
		default:
			return false;
	}
}

ssize_t get_final_offset(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP: {
			loop_info info = get_loop_info(op);
			assert(!info.uncertain_forwards && !info.uncertain_backwards);
			return 0;
		}
		case BF_OP_SKIP:
			assert(!"No way to get the offset of this opcode");
			return 0;
		case BF_OP_ALTER:
			return op->offset;
		case BF_OP_BOUNDS_CHECK:
		case BF_OP_IN:
		case BF_OP_OUT:
		case BF_OP_SET:
		case BF_OP_MULTIPLY:
			return 0;
		default:
			assert(!"Unexpected opcode");
			return 0;
	}
}

ssize_t get_max_offset(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_SKIP:
			assert(!"No way to get the offset of this opcode");
			return 0;
		case BF_OP_ALTER:
			return op->offset;
		case BF_OP_BOUNDS_CHECK:
		case BF_OP_IN:
		case BF_OP_OUT:
			return 0;
		case BF_OP_SET:
			return op->offset;
		case BF_OP_MULTIPLY:
			if (op->offset > 0)
				return op->offset;
			else
				return 0;
		default:
			assert(!"Unexpected opcode");
			return 0;
	}
}

ssize_t get_min_offset(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_SKIP:
			assert(!"No way to get the offset of this opcode");
			return 0;
		case BF_OP_ALTER:
			return op->offset;
		case BF_OP_BOUNDS_CHECK:
		case BF_OP_IN:
		case BF_OP_OUT:
			return 0;
		case BF_OP_SET:
			return op->offset;
		case BF_OP_MULTIPLY:
			if (op->offset < 0)
				return op->offset;
			else
				return 0;
		default:
			assert(!"Unexpected opcode");
			return 0;
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
				} else if (child_info.offset_upper > 0) {
					info.offset_upper += child_info.offset_upper;
				}
				if (child_info.inner_uncertain_backwards) {
					info.inner_uncertain_backwards = true;
				} else if (child_info.offset_lower < 0) {
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

offset_access offset_might_be_accessed(ssize_t offset, bf_op_builder *restrict arr, size_t start, size_t end) {
	assert(end <= arr->len);

	for (size_t pos = start; pos < end; pos++) {
		bf_op *op = &arr->ops[pos];
		switch (op->op_type) {
			case BF_OP_IN:
				if (offset == 0)
					return (offset_access) {.pos = pos, .write = true};
				break;
			case BF_OP_OUT:
				if (offset == 0)
					return (offset_access) {.pos = pos, .read = true};
				break;
			case BF_OP_ALTER:
				offset -= op->offset;
				if (offset == 0 && op->amount != 0)
					return (offset_access) {.pos = pos, .read = true, .write = true};
				break;
			case BF_OP_LOOP: {
				loop_info info = get_loop_info(op);
				if (info.uncertain_forwards || info.uncertain_backwards)
					return (offset_access) {.pos = pos, .uncertain = true};
				if (offset == 0)
					return (offset_access) {.pos = pos, .read = true};
				offset_access access = offset_might_be_accessed(offset, &op->children, 0, op->children.len);
				if (access.pos != -1) {
					access.pos = pos;
					return access;
				}
				break;
			}
			case BF_OP_SET:
				if (offset >= 0 && offset <= op->offset)
					return (offset_access) {.pos = pos, .write = true};
				break;
			case BF_OP_MULTIPLY:
				if (offset == 0)
					return (offset_access) {.pos = pos, .read = true};
				if (offset == op->offset)
					return (offset_access) {.pos = pos, .read = true, .write = true};
				break;
			case BF_OP_SKIP:
				return (offset_access) {.pos = pos, .uncertain = true};
			default:
				assert(!"Unexpected opcode");
				return (offset_access) {.pos = pos, .uncertain = true};
		}
	}

	return (offset_access) {.pos = -1};
}
