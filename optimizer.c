#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "optimizer.h"
#include "optimizer_helpers.h"
#include "parser.h"

/*
 * Determines whether a loop is "alter-only" or not.
 * That is, returns true if and only if the loop contains no
 * non-BF_OP_ALTER ops.
 */
static bool is_loop_alter_only(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.len == 0 || op->children.ops != NULL);

	for (size_t i = 0; i < op->children.len; i++)
		if (op->children.ops[i].op_type != BF_OP_ALTER)
			return false;

	return true;
}

static bool move_addition(bf_op_builder *restrict arr, size_t add_pos) {
	assert(add_pos < arr->len);
	assert(arr->ops[add_pos].op_type == BF_OP_ALTER);
	assert(arr->ops[add_pos].offset == 0);

	size_t final_pos = add_pos + 1;

	offset_access access = offset_might_be_accessed(arr, (offset_access) {.offset = 0, .pos = final_pos});
	if (access.pos != -1 && arr->ops[access.pos].op_type == BF_OP_ALTER) {
		final_pos = access.pos;
	} else {
		bool found_spot = false;

		if (access.pos == -1)
			access.pos = arr->len - 1;
		if (access.uncertain)
			access.pos--;

		ssize_t offset = 0;
		for (; final_pos <= (size_t) access.pos; final_pos++) {
			bf_op *restrict op = &arr->ops[final_pos];
			if (op->op_type == BF_OP_ALTER && op->offset == -offset) {
				found_spot = true;
				break;
			}
			offset += get_final_offset(op);
		}

		if (!found_spot) return false;
	}

	assert(arr->ops[final_pos].op_type == BF_OP_ALTER);
	arr->ops[final_pos].amount += arr->ops[add_pos].amount;
	remove_bf_ops(arr, add_pos, 1);
	return true;
}

static void make_offsets_absolute(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.len == 0 || op->children.ops != NULL);

	ssize_t current_offset = 0;
	for (size_t i = 0; i < op->children.len; i++) {
		bf_op *curr_op = &op->children.ops[i];
		assert(curr_op->op_type == BF_OP_ALTER);

		current_offset += curr_op->offset;
		curr_op->offset = current_offset;
	}
}

static void make_offsets_relative(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.len == 0 || op->children.ops != NULL);

	ssize_t current_offset = 0;
	for (size_t i = 0; i < op->children.len; i++) {
		bf_op *curr_op = &op->children.ops[i];
		assert(curr_op->op_type == BF_OP_ALTER);

		curr_op->offset -= current_offset;
		current_offset += curr_op->offset;
	}
}

static bool make_loop_into_multiply(bf_op_builder *ops) {
	assert(ops != NULL);
	assert(ops->ops != NULL);
	assert(ops->len > 0);

	size_t my_op_index = ops->len - 1;
	bf_op *op = &ops->ops[my_op_index];

	assert(op->op_type == BF_OP_LOOP);

	// Infinite loops don't apply, of course
	if (op->children.len == 0) return false;

	// Nor do loops which do things like I/O or nested loops
	if (!is_loop_alter_only(op)) return false;

	make_offsets_absolute(op);

	if (op->children.ops[op->children.len - 1].offset != 0) {
		// "unbalanced" loop, don't change it
error:
		make_offsets_relative(op);
		return false;
	}

	// ensure the loop variable decreases by one each time
	cell_int loop_increment = 0;
	for (size_t i = 0; i < op->children.len; i++)
		if (op->children.ops[i].offset == 0)
			loop_increment += op->children.ops[i].amount;

	bool positive;
	if (loop_increment == (cell_int)-1) positive = true; // Take away 1 each time = use positive value of cell as loop counter
	else if (loop_increment == 1) positive = false;
	else goto error;

	// Go along finding and removing all alter ops with the same offset
	while (op->children.len) {
		ssize_t target_offset = op->children.ops[0].offset;
		cell_int final_amount = op->children.ops[0].amount;

		remove_bf_ops(&op->children, 0, 1);
		for (size_t i = 0; i < op->children.len; i++) {
			if (op->children.ops[i].offset != target_offset)
				continue;

			final_amount += op->children.ops[i].amount;
			remove_bf_ops(&op->children, i--, 1);
		}

		if (!positive) final_amount = -final_amount;
		if (target_offset != 0) {
			*alloc_bf_op(ops) = (bf_op) {
				.op_type = BF_OP_MULTIPLY,
				.offset = target_offset,
				.amount = final_amount,
			};
			op = &ops->ops[my_op_index];
		}
	}

	remove_bf_ops(ops, my_op_index, 1);

	*alloc_bf_op(ops) = (bf_op) {
		.op_type = BF_OP_SET,
		.offset = 0,
		.amount = 0,
	};
	return true;
}

static bool can_merge_set_ops(bf_op_builder *restrict arr, size_t pos) {
	// We need a lookbehind of 2
	if (pos < 2) return false;

	// Looking for SET ALTER SET (e.g. "[-]>[-]")
	if (arr->ops[pos].op_type != BF_OP_SET) return false;
	if (arr->ops[pos - 1].op_type != BF_OP_ALTER) return false;
	if (arr->ops[pos - 2].op_type != BF_OP_SET) return false;

	// The alter must bring us exactly 1 cell past the first (multi-)SET
	// (also, we don't care about its amount; a SET overwrites it)
	if (arr->ops[pos - 1].offset != 1 + arr->ops[pos - 2].offset) return false;
	// The amounts to SET to must be identical
	if (arr->ops[pos - 2].amount != arr->ops[pos].amount) return false;

	return true;
}

static bool is_redundant_set_lookahead(bf_op_builder *restrict arr, size_t pos) {
	bf_op *op = &arr->ops[pos];
	if (op->op_type != BF_OP_SET) return false;

	if (pos + 1 < arr->len) {
		bf_op *next = &arr->ops[pos + 1];
		// If there's a SET after us, covering a wider or equal range of
		// offsets, this one isn't necessary.
		if (next->op_type == BF_OP_SET && op->offset <= next->offset)
			return true;
	}

	return false;
}

static bool is_redundant_alter(bf_op_builder *arr, size_t pos) {
	bf_op *op = &arr->ops[pos];
	if (op->op_type != BF_OP_ALTER) return false;

	// If this ALTER alters nothing, it's redundant
	if (op->offset == 0 && op->amount == 0) return true;

	return false;
}

static bool is_redundant(bf_op_builder *arr, size_t pos) {
	if (is_redundant_alter(arr, pos)) return true;

	bf_op *op = &arr->ops[pos];
	if (op->definitely_zero && expects_nonzero(op)) return true;

	return false;
}

static void mark_as_zero(bf_op_builder *ops, size_t initial_pos, bool all_zeros) {
	for (size_t pos = initial_pos; pos < ops->len; pos++) {
		bf_op *op = &ops->ops[pos];

		op->definitely_zero = true;

		if (!ensures_zero(op)) {
			if (writes_cell(op))
				break;

			if (moves_tape(op) && !all_zeros)
				break;
		}
	}
}

static void mark_as_nonzero(bf_op_builder *ops, size_t initial_pos) {
	for (size_t pos = initial_pos; pos < ops->len; pos++) {
		bf_op *op = &ops->ops[pos];

		op->definitely_nonzero = true;

		if (!ensures_nonzero(op)) {
			if (writes_cell(op))
				break;

			if (moves_tape(op))
				break;
		}
	}
}

static bool can_merge_alter(bf_op_builder *ops, size_t pos) {
	if (pos < 1) return false;

	bf_op *left = &ops->ops[pos - 1];
	bf_op *right = &ops->ops[pos];

	if (right->op_type != BF_OP_ALTER) return false;
	if (left->op_type != BF_OP_ALTER) return false;

	if (left->amount != 0 && right->offset != 0) return false;

	return true;
}

static void merge_alter(bf_op_builder *ops, size_t pos) {
	assert(can_merge_alter(ops, pos));

	bf_op *left = &ops->ops[pos - 1];
	bf_op *right = &ops->ops[pos];

	left->offset += right->offset;
	left->amount += right->amount;

	remove_bf_ops(ops, pos, 1);
}

static bool loops_exactly_once(bf_op *op) {
	if (op->op_type != BF_OP_LOOP) return false;
	if (!op->definitely_nonzero) return false;
	return get_loop_info(op).loops_once_at_most;
}

static void remove_looping(bf_op_builder *ops, size_t loop_pos) {
	size_t loop_len = ops->ops[loop_pos].children.len;

	bf_op *inlined_ops = insert_bf_ops(ops, loop_pos, loop_len);
	loop_pos += loop_len;
	bf_op *loop = &ops->ops[loop_pos];
	memcpy(inlined_ops, loop->children.ops, loop_len * sizeof *inlined_ops);

	// remove_bf_ops does a deep free of the loop structure, so prevent that by
	// freeing and discarding the structure ourselves (but in a shallow way)
	free(loop->children.ops);
	loop->children = (bf_op_builder) {0};

	remove_bf_ops(ops, loop_pos, 1);
}

static void correct_knowledge_for_merge_right(bf_op_builder *arr, size_t pos) {
	// Since some optimizations look ahead to determine if a later op makes an
	// earlier one redundant, and remove the earlier op, the next op's
	// assumptions about 'definitely_zero' / 'definitely_nonzero' will become
	// incorrect. They should be copied over from the first op.
	// The rest will be propagated via mark_as_zero / mark_as_nonzero.
	assert(pos + 1 < arr->len);
	bf_op *op = &arr->ops[pos];
	op[1].definitely_zero = op[0].definitely_zero;
	op[1].definitely_nonzero = op[0].definitely_nonzero;
}

static bool lookahead_merge_write(bf_op_builder *arr, size_t pos) {
	// Merge write operations by looking ahead to see if/when they are made
	// redundant or built upon.
	assert(arr != NULL);
	assert(pos < arr->len);

	bf_op *op = &arr->ops[pos];

	if (!writes_cell(op)) return false;
	if (performs_io(op)) return false;

	assert(op->op_type != BF_OP_SKIP);

	if (op->op_type == BF_OP_LOOP)
		return false;  // No sensible way to merge loops with other opcodes

	// TODO: This should probably be abstracted into an optimizer_helpers function
	ssize_t write_at;
	switch (op->op_type) {
		case BF_OP_MULTIPLY:
		case BF_OP_ALTER:
			write_at = op->offset;
			break;
		case BF_OP_SET:
			if (op->offset != 0) {
				// This doesn't work for checking instructions which affect
				// multiple offsets yet
				return false;
			}
			write_at = 0;
			break;
		default:
			assert(!"Unexpected opcode");
			return false;
	}

	offset_access access = offset_might_be_accessed(arr, (offset_access) {
			.offset = write_at - get_final_offset(op),
			.pos = pos + 1
		});

	if (access.maybe_read || access.maybe_write)
		// If it's not certain whether it's accessed, for safety's sake don't
		// merge it with other opcodes
		return false;

	if (pos > 0 && arr->ops[pos - 1].op_type == BF_OP_MULTIPLY && op->op_type == BF_OP_SET)
		return false;  // Don't remove the SET from the end of a MULTIPLY

	if (access.write && !access.read) {
		// If this op is overwritten by a later op, this op can go.
		if (op->op_type == BF_OP_ALTER) {
			// ALTERs might move the pointer, so just blank their amount (the
			// peephole optimizer will pick up any empty ALTERs left over)
			op->amount = 0;
		} else {
			correct_knowledge_for_merge_right(arr, pos);
			remove_bf_ops(arr, pos, 1);
		}
		return true;
	}

	if (access.write && access.read && (op->op_type == BF_OP_SET || op->op_type == BF_OP_ALTER)) {
		// If we're a SET or ALTER, we can feel free to assimilate
		// later ALTER ops if nothing has touched our cell in the meantime.
		// This deletes the later op.
		bf_op *accessor = &arr->ops[access.pos];
		if (accessor->op_type == BF_OP_ALTER) {
			op->amount += accessor->amount;
			if (accessor->offset == 0) {
				remove_bf_ops(arr, access.pos, 1);
			} else {
				accessor->amount = 0;
			}
			return true;
		}
	}
	return false;
}

static size_t peephole_optimize(bf_op_builder *ops, size_t i, bool starts_nonzero) {
	bf_op *child = &ops->ops[i];

	if (i > 0) {
		bf_op *prev = &ops->ops[i - 1];
		if (prev->definitely_zero)
			mark_as_zero(ops, i - 1, false);
		else if (ensures_zero(prev))
			mark_as_zero(ops, i, false);

		if (prev->definitely_nonzero)
			mark_as_nonzero(ops, i - 1);
		else if (ensures_nonzero(prev))
			mark_as_nonzero(ops, i);
	} else if (starts_nonzero) {
		mark_as_nonzero(ops, 0);
	}

	assert(!(child->definitely_zero && child->definitely_nonzero));

	if (is_redundant(ops, i)) {
		remove_bf_ops(ops, i, 1);
		return 1;
	} else if (is_redundant_set_lookahead(ops, i)) {
		correct_knowledge_for_merge_right(ops, i);
		remove_bf_ops(ops, i, 1);
		return 1;
	} else if (lookahead_merge_write(ops, i)) {
		return 1;
	} else if (can_merge_alter(ops, i)) {
		merge_alter(ops, i);
		return 2;
	} else if (child->op_type == BF_OP_MULTIPLY && child->offset == 0) {
		child->op_type = BF_OP_LOOP;
		child->children.ops = NULL;
		child->children.len = 0;
		return 1;
	} else if (child->op_type == BF_OP_ALTER && child->offset == 0) {
		if (child->definitely_zero) {
			child->op_type = BF_OP_SET;
			return 1;
		} else if (move_addition(ops, i))
			return 1;
	} else if (can_merge_set_ops(ops, i)) {
		ssize_t old_offset = ops->ops[i - 1].offset;
		ops->ops[i - 2].offset += child->offset + 1;
		i--;
		remove_bf_ops(ops, i, 2);
		if (i < ops->len && ops->ops[i].op_type == BF_OP_ALTER) {
			ops->ops[i].offset += old_offset;
		} else {
			*insert_bf_ops(ops, i, 1) = (bf_op) {
				.op_type = BF_OP_ALTER,
				.offset = old_offset,
				.amount = 0,
			};
		}
		return 3;
	} else if (loops_exactly_once(child)) {
		remove_looping(ops, i);
		return 1;
	}
	return 0;
}

static void peephole_optimize_all(bf_op_builder *ops, bool starts_nonzero) {
	bool optimized_this_time = true;
	while(optimized_this_time) {
		optimized_this_time = false;
		for (size_t i = 0; i < ops->len; i++) {
			size_t changed_insns = peephole_optimize(ops, i, starts_nonzero);
			if (changed_insns) {
				i -= changed_insns;
				optimized_this_time = true;
			}
		}
	}
}

void optimize_loop(bf_op_builder *ops) {
	bf_op *op = &ops->ops[ops->len - 1];

	// Peephole optimizations that can't be done while initially building the loop's AST
	peephole_optimize_all(&op->children, true);

	// Find common types of loop
	if (op->children.len == 1
			&& op->children.ops[0].op_type == BF_OP_ALTER
			&& op->children.ops[0].amount == 0) {
		ssize_t offset = op->children.ops[0].offset;
		free_bf_op_children(op);
		op->op_type = BF_OP_SKIP;
		op->offset = offset;
	} else if (make_loop_into_multiply(ops)) {
		return;
	}
}

void optimize_root(bf_op_builder *ops) {
	// We know the whole tape is zeros when the program starts
	mark_as_zero(ops, 0, true);

	// Run peephole optimizer (required for flattener to function at all)
	peephole_optimize_all(ops, false);
}

static bool directions_agree(ssize_t a, ssize_t b) {
	if (a < 0 && b < 0) return true;
	if (a > 0 && b > 0) return true;
	return  false;
}

static void make_bound_check(bf_op_builder *ops, size_t pos, ssize_t bound) {
	*insert_bf_ops(ops, pos, 1) = (bf_op) {
		.op_type = BF_OP_BOUNDS_CHECK,
		.offset = bound,
	};
}

static size_t check_for_bound_check(bf_op_builder *ops_arr, size_t index, int direction) {
	assert(direction == 1 || direction == -1);

	for (size_t j = index; j < ops_arr->len && j < index + 2; j++) {
		bf_op *op = &ops_arr->ops[j];
		if (op->op_type == BF_OP_BOUNDS_CHECK && directions_agree(op->offset, direction))
			return j;
	}
	return (size_t) -1;
}

static void overwrite_bound_check_if_necessary(bf_op *bound_check_op, ssize_t bound) {
	// Only replace the bounds check if our bound is "farther" in the
	// direction the bounds check is already checking in.
	if (directions_agree(bound - bound_check_op->offset, bound_check_op->offset)) {
		bound_check_op->offset = bound;
	}
}

static size_t reuse_or_make_bound_check(bf_op_builder *ops, size_t index, int bound, int direction) {
	assert(bound != 0);
	assert(directions_agree(bound, direction));
	(void) direction;  // direction is unused in NDEBUG builds

	size_t bound_check = check_for_bound_check(ops, index, bound > 0 ? 1 : -1);
	if (bound_check == (size_t) -1) {
		make_bound_check(ops, index, bound);
		return 1;
	} else {
		bf_op *bound_check_op = &ops->ops[bound_check];

		overwrite_bound_check_if_necessary(bound_check_op, bound);
		return 0;
	}
}

static size_t pull_bound_check(bf_op_builder *ops, size_t *call_op_index, size_t i, ssize_t current_offset, int direction) {
	size_t shift = 0;
	bf_op *op = &ops->ops[*call_op_index];
	assert(op->op_type == BF_OP_LOOP);
	size_t inner_bound_check = check_for_bound_check(&op->children, 0, direction);
	if (inner_bound_check != (size_t)-1) {
		ssize_t inner_bound_offset = op->children.ops[inner_bound_check].offset + current_offset;
		// Does the inner bound check still point the same way after adding our current offset?
		if (directions_agree(inner_bound_offset, direction)) {
			// The outer bound check may need to be created or updated
			size_t bound_check = check_for_bound_check(ops, i, direction);
			if (bound_check == (size_t)-1) {
				make_bound_check(ops, i, inner_bound_offset);
				shift = 1;
				(*call_op_index)++;

				op = &ops->ops[*call_op_index]; // we moved due to make_bound_check's realloc and memmove

				bound_check = i;
			}
			bf_op *restrict bound_op = &ops->ops[bound_check];
			assert(directions_agree(bound_op->offset, direction));

			overwrite_bound_check_if_necessary(bound_op, inner_bound_offset);
		}
		remove_bf_ops(&op->children, inner_bound_check, 1);
	}
	return shift;
}

#ifndef NDEBUG
static bool have_bound_at(bf_op_builder *arr, size_t where, int direction) {
	if ((ssize_t) where < 0) return false;
	assert(where < arr->len);

	bf_op *op = &arr->ops[where];
	if (op->op_type == BF_OP_BOUNDS_CHECK) {
		assert(op->offset != 0);
		return directions_agree(op->offset, direction);
	}
	return false;
}
#endif

void add_bounds_checks(bf_op_builder *ops) {
	assert(ops != NULL);
	if (ops->len == 0)
		return;

	assert(ops->ops != NULL);

	size_t last_certain_forwards = 0, last_certain_backwards = 0;

	ssize_t curr_off_fwd = 0; // Current offset since last forwards uncertainty
	ssize_t curr_off_bck = 0;
	for (size_t pos = 0; pos < ops->len;) {
		ssize_t max_bound = 0, min_bound = 0;

		while (pos < ops->len) {
			bf_op *op = &ops->ops[pos];
			if (op->op_type == BF_OP_LOOP || op->op_type == BF_OP_SKIP)
				break;

			ssize_t high_bound = curr_off_fwd + get_max_offset(op);
			if (high_bound > max_bound) max_bound = high_bound;

			ssize_t low_bound = curr_off_bck + get_max_offset(op);
			if (low_bound < min_bound) min_bound = low_bound;

			curr_off_fwd += get_final_offset(op);
			curr_off_bck += get_final_offset(op);
			pos++;
		}

		if (max_bound) {
			assert(!have_bound_at(ops, last_certain_forwards - 1, 1));
			size_t shift = reuse_or_make_bound_check(ops, last_certain_forwards, max_bound, 1);
			pos += shift;
			if (last_certain_backwards > last_certain_forwards)
				last_certain_backwards += shift;
			assert(!have_bound_at(ops, last_certain_forwards - 1, 1));
		}
		if (min_bound) {
			assert(!have_bound_at(ops, last_certain_backwards - 1, -1));
			size_t shift = reuse_or_make_bound_check(ops, last_certain_backwards, min_bound, -1);
			pos += shift;
			if (last_certain_forwards > last_certain_backwards)
				last_certain_forwards += shift;
			assert(!have_bound_at(ops, last_certain_backwards - 1, -1));
		}

		assert(pos <= ops->len);
		if (pos >= ops->len)
			break;

		size_t this_op_pos = pos++;
		bf_op *op = &ops->ops[this_op_pos];
		if (op->op_type == BF_OP_LOOP) {
			add_bounds_checks(&op->children);

			// Serious hacks round 2: pull bounds checks from the beginning of the loop
			loop_info info = get_loop_info(op);

			assert(!have_bound_at(ops, last_certain_backwards - 1, -1));
			if (!info.uncertain_backwards || (!info.inner_uncertain_backwards && info.loops_once_at_most)) {
				size_t shift = pull_bound_check(ops, &this_op_pos, last_certain_backwards, curr_off_bck, -1);
				pos += shift;
				if (last_certain_forwards > last_certain_backwards)
					last_certain_forwards += shift;

				curr_off_bck += info.offset_lower;
				if (curr_off_bck < 0) {
					assert(check_for_bound_check(ops, last_certain_backwards, -1) != (size_t) -1);
					assert(ops->ops[check_for_bound_check(ops, last_certain_backwards, -1)].offset <= curr_off_bck);
				}
			} else {
				last_certain_backwards = pos;
				curr_off_bck = 0;
			}
			assert(!have_bound_at(ops, last_certain_backwards - 1, -1));

			assert(!have_bound_at(ops, last_certain_forwards - 1, 1));
			if (!info.uncertain_forwards || (!info.inner_uncertain_forwards && info.loops_once_at_most)) {
				size_t shift = pull_bound_check(ops, &this_op_pos, last_certain_forwards, curr_off_fwd, 1);
				pos += shift;
				if (last_certain_backwards > last_certain_forwards)
					last_certain_backwards += shift;

				curr_off_fwd += info.offset_upper;
				if (curr_off_fwd > 0) {
					assert(check_for_bound_check(ops, last_certain_forwards, 1) != (size_t) -1);
					assert(ops->ops[check_for_bound_check(ops, last_certain_forwards, 1)].offset >= curr_off_fwd);
				}
			} else {
				last_certain_forwards = pos;
				curr_off_fwd = 0;
			}
			assert(!have_bound_at(ops, last_certain_forwards - 1, 1));
		} else if (op->op_type == BF_OP_SKIP) {
			if (op->offset > 0) {
				last_certain_forwards = pos;
				curr_off_fwd = 0;
			} else {
				last_certain_backwards = pos;
				curr_off_bck = 0;
			}
		}
	}
	// Make this loop a little smaller
	ops->alloc = ops->len;
	ops->ops = realloc(ops->ops, ops->len * sizeof *ops->ops);
}
