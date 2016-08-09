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

bool performs_io(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_IN:
		case BF_OP_OUT:
			return true;
		default:
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

offset_access offset_might_be_accessed(bf_op_builder *restrict arr, offset_access access) {
	assert(access.pos >= 0);
	assert(arr != NULL);
	assert(arr->len == 0 || arr->ops != NULL);

	for (; (size_t)access.pos < arr->len; access.pos++) {
		bf_op *op = &arr->ops[(size_t)access.pos];
		switch (op->op_type) {
			case BF_OP_IN:
				if (access.offset == 0) {
					access.write = true;
					return access;
				}
				break;
			case BF_OP_OUT:
				if (access.offset == 0) {
					access.read = true;
					return access;
				}
				break;
			case BF_OP_ALTER:
				access.offset -= op->offset;
				if (access.offset == 0 && op->amount != 0) {
					access.read = access.write = true;
					return access;
				}
				break;
			case BF_OP_LOOP: {
				loop_info info = get_loop_info(op);
				if (info.uncertain_forwards || info.uncertain_backwards) {
					access.uncertain = true;
					return access;
				}
				if (access.offset == 0) {
					access.read = true;
					return access;
				}
				offset_access child_access = offset_might_be_accessed(&op->children, (offset_access) {.offset = access.offset});
				assert(!child_access.uncertain);
				if (child_access.pos != -1) {
					access.maybe_read |= child_access.read;
					access.maybe_write |= child_access.write;
					return access;
				}
				break;
			}
			case BF_OP_SET:
				if (access.offset >= 0 && access.offset <= op->offset) {
					access.write = true;
					return access;
				}
				break;
			case BF_OP_MULTIPLY:
				assert(op->offset != 0);
				if (access.offset == 0) {
					access.read = true;
					return access;
				}
				if (access.offset == op->offset) {
					access.read = access.write = true;
					return access;
				}
				break;
			case BF_OP_SKIP:
				access.uncertain = true;
				return access;
			default:
				assert(!"Unexpected opcode");
				access.uncertain = true;
				return access;
		}
	}

	access.pos = -1;
	return access;
}
