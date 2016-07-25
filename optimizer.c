#include <assert.h>
#include <string.h>
#include "optimizer.h"
#include "parser.h"

/*
 * Determines whether a loop is "alter-only" or not.
 * That is, returns true if and only if the loop contains no
 * non-BF_OP_ALTER ops.
 *
 */
bool is_loop_alter_only(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.len == 0 || op->children.ops != NULL);

	for (size_t i = 0; i < op->children.len; i++)
		if (op->children.ops[i].op_type != BF_OP_ALTER)
			return false;

	return true;
}

#define UNCERTAIN_FORWARDS  1
#define UNCERTAIN_BACKWARDS 2
#define UNCERTAIN_BOTH      3
#define UNCERTAINTY_PRESENT 4
/*
 * Returns:
 * 0                   if the loop does not end up moving the data pointer
 * UNCERTAIN_FORWARDS  if the loop moves it forwards by some degree
 * UNCERTAIN_BACKWARDS if the loop moves it backwards by some degree
 * UNCERTAIN_BOTH      if it's impossible to tell
 */
int get_loop_balance(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->children.len == 0 || op->children.ops != NULL);

	int uncertainty = op->uncertainty;
	if (uncertainty)
		return uncertainty & UNCERTAIN_BOTH;

	ssize_t balance = 0;

	for (size_t i = 0; i < op->children.len; i++) {
		bf_op *child = &op->children.ops[i];
		if (child->op_type == BF_OP_ALTER)
			balance += child->offset;
		else if (child->op_type == BF_OP_SKIP) {
			assert(child->offset != 0);
			if (child->offset > 0)
				uncertainty |= UNCERTAIN_FORWARDS;
			else
				uncertainty |= UNCERTAIN_BACKWARDS;

			if (uncertainty == UNCERTAIN_BOTH) break;
		} else if (child->op_type == BF_OP_LOOP) {
			uncertainty |= get_loop_balance(child);
			if (uncertainty == UNCERTAIN_BOTH) break;
		}
	}

	if (balance > 0)
		uncertainty |= UNCERTAIN_FORWARDS;
	else if (balance < 0)
		uncertainty |= UNCERTAIN_BACKWARDS;
	op->uncertainty = uncertainty | UNCERTAINTY_PRESENT;
	return uncertainty;
}

void make_offsets_absolute(bf_op *restrict op) {
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

void make_offsets_relative(bf_op *restrict op) {
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

bool make_loop_into_multiply(bf_op_builder *ops) {
	assert(ops != NULL);
	assert(ops->out.ops != NULL);
	assert(ops->out.len > 0);

	size_t my_op_index = ops->out.len - 1;
	bf_op *op = &ops->out.ops[my_op_index];

	assert(op->op_type == BF_OP_LOOP);

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

	if (loop_increment != (cell_int)-1) goto error;

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

		if (target_offset != 0) {
			*alloc_bf_op(ops) = (bf_op) {
				.op_type = BF_OP_MULTIPLY,
				.offset = target_offset,
				.amount = final_amount,
			};
			op = &ops->out.ops[my_op_index];
		}
	}

	free(op->children.ops);
	remove_bf_ops(&ops->out, my_op_index, 1);

	*alloc_bf_op(ops) = (bf_op) {
		.op_type = BF_OP_SET,
		.offset = 0,
		.amount = 0,
	};
	return true;
}

void optimize_loop(bf_op_builder *ops) {
	bf_op *op = &ops->out.ops[ops->out.len - 1];
	// Peephole optimizations that can't be done during build time
	for (size_t i = 0; i < op->children.len; i++) {
		bf_op *child = &op->children.ops[i];
		if (child->op_type == BF_OP_ALTER
				&& child->offset == 0 && child->amount == 0)
			remove_bf_ops(&op->children, i--, 1);
		else if (child->op_type == BF_OP_MULTIPLY && child->offset == 0) {
			child->op_type = BF_OP_LOOP;
			child->children.ops = NULL;
			child->children.len = 0;
		}
	}

	// Find common types of loop
	if (op->children.len == 1
			&& op->children.ops[0].op_type == BF_OP_ALTER
			&& op->children.ops[0].amount == 0) {
		ssize_t offset = op->children.ops[0].offset;
		free(op->children.ops);
		op->op_type = BF_OP_SKIP;
		op->offset = offset;
	} else if (make_loop_into_multiply(ops)) {
		return;
	}
}

static void make_bound_check(bf_op_builder *ops, size_t pos, ssize_t bound) {
	*insert_bf_op(ops, pos) = (bf_op) {
		.op_type = BF_OP_BOUNDS_CHECK,
		.offset = bound,
	};
}

static size_t check_for_bound_check(bf_op_array *ops_arr, size_t index, int direction) {
	assert(direction == 1 || direction == -1);

	for (size_t j = index; j < ops_arr->len && j < index + 2; j++) {
		bf_op *op = &ops_arr->ops[j];
		if (op->op_type == BF_OP_BOUNDS_CHECK
				&& ((op->offset < 0 && direction == -1)
					|| (op->offset > 0 && direction == 1)))
			return j;
	}
	return (size_t) -1;
}

static size_t reuse_or_make_bound_check(bf_op_builder *ops, size_t index, int bound, int direction) {
	assert(bound != 0);
	size_t bound_check = check_for_bound_check(&ops->out, index, bound > 0 ? 1 : -1);
	if (bound_check == (size_t) -1) {
		make_bound_check(ops, index, bound);
		return 1;
	} else {
		bf_op *bound_check_op = &ops->out.ops[bound_check];
		if ((bound > bound_check_op->offset && direction > 0)
				|| (bound < bound_check_op->offset && direction < 0)) {
			bound_check_op->offset = bound;
		}
		return 0;
	}
}

static size_t pull_bound_check(bf_op_builder *ops, size_t *call_op_index, size_t i, ssize_t current_offset, int direction) {
	size_t shift = 0;
	bf_op *op = &ops->out.ops[*call_op_index];
	assert(op->op_type == BF_OP_LOOP);
	size_t inner_bound_check = check_for_bound_check(&op->children, 0, direction);
	if (inner_bound_check != (size_t)-1) {
		ssize_t inner_bound_offset = op->children.ops[inner_bound_check].offset + current_offset;
		// Bounds check found, find somewhere to put it (or make that place)
		if ((inner_bound_offset > 0 && direction > 0)
				|| (inner_bound_offset < 0 && direction < 0)) {
			// The bounds check may need to be created or updated
			size_t bound_check = check_for_bound_check(&ops->out, i, direction);
			if (bound_check == (size_t)-1) {
				make_bound_check(ops, i, inner_bound_offset);
				shift = 1;
				(*call_op_index)++;

				op = &ops->out.ops[*call_op_index]; // we moved due to make_bound_check's realloc and memmove

				bound_check = i;
			}
			bf_op *restrict bound_op = &ops->out.ops[bound_check];
			assert((bound_op->offset > 0 && direction > 0)
					|| (bound_op->offset < 0 && direction < 0));
			if ((direction > 0 && inner_bound_offset > bound_op->offset)
					|| (direction < 0 && inner_bound_offset < bound_op->offset)) {
				bound_op->offset = inner_bound_offset;
				assert(bound_op->offset != 0);
			}
		}
		remove_bf_ops(&op->children, inner_bound_check, 1);
	}
	return shift;
}

#ifndef NDEBUG
static bool have_bound_at(bf_op_array *arr, size_t where, int direction) {
	if ((ssize_t) where < 0) return false;
	assert(where < arr->len);

	bf_op *op = &arr->ops[where];
	if (op->op_type == BF_OP_BOUNDS_CHECK) {
		assert(op->offset != 0);
		if (op->offset < 0) {
			return direction < 0;
		} else {
			return direction > 0;
		}
	}
	return false;
}
#endif

void add_bounds_checks(bf_op_builder *ops) {
	assert(ops != NULL);
	if (ops->out.len == 0)
		return;

	assert(ops->out.ops != NULL);

	size_t last_certain_forwards = 0, last_certain_backwards = 0;

	ssize_t curr_off_fwd = 0; // Current offset since last forwards uncertainty
	ssize_t curr_off_bck = 0;
	for (size_t i = 0; i < ops->out.len;) {
		ssize_t max_bound = 0, min_bound = 0;
		size_t end_pos = i;

		while (end_pos < ops->out.len) {
			bf_op *op = &ops->out.ops[end_pos];
			if (op->op_type == BF_OP_LOOP || op->op_type == BF_OP_SKIP)
				break;

			if (op->op_type == BF_OP_ALTER || op->op_type == BF_OP_MULTIPLY) {
				curr_off_fwd += op->offset;
				curr_off_bck += op->offset;
			}

			if (curr_off_bck < min_bound)
				min_bound = curr_off_bck;
			else if (curr_off_fwd > max_bound)
				max_bound = curr_off_fwd;

			if (op->op_type == BF_OP_MULTIPLY) {
				// Revert offset again, as the instruction does
				curr_off_fwd -= op->offset;
				curr_off_bck -= op->offset;
			}

			end_pos++;
		}

		if (max_bound) {
			assert(!have_bound_at(&ops->out, last_certain_forwards - 1, 1));
			size_t shift = reuse_or_make_bound_check(ops, last_certain_forwards, max_bound, 1);
			end_pos += shift;
			if (last_certain_backwards > last_certain_forwards)
				last_certain_backwards += shift;
			assert(!have_bound_at(&ops->out, last_certain_forwards - 1, 1));
		}
		if (min_bound) {
			assert(!have_bound_at(&ops->out, last_certain_backwards - 1, -1));
			size_t shift = reuse_or_make_bound_check(ops, last_certain_backwards, min_bound, -1);
			end_pos += shift;
			if (last_certain_forwards > last_certain_backwards)
				last_certain_forwards += shift;
			assert(!have_bound_at(&ops->out, last_certain_backwards - 1, -1));
		}

		assert(end_pos <= ops->out.len);
		if (end_pos >= ops->out.len)
			break;

		size_t this_op_pos = end_pos++;
		bf_op *op = &ops->out.ops[this_op_pos];
		if (op->op_type == BF_OP_LOOP) {
			// Playing with fire...
			// Wrap op children in a reallocatable structure
			bf_op_builder builder = {
				.out = op->children,
				.alloc = op->children.len,
			};
			add_bounds_checks(&builder);
			op->children = builder.out;

			// Serious hacks round 2: pull bounds checks from the beginning of the loop
			int uncertainty = get_loop_balance(op);

			assert(!have_bound_at(&ops->out, last_certain_backwards - 1, -1));
			if ((uncertainty & UNCERTAIN_BACKWARDS) == 0) {
				size_t shift = pull_bound_check(ops, &this_op_pos, last_certain_backwards, curr_off_bck, -1);
				end_pos += shift;
				if (last_certain_forwards > last_certain_backwards)
					last_certain_forwards += shift;
			} else {
				last_certain_backwards = end_pos;
				curr_off_bck = 0;
			}
			assert(!have_bound_at(&ops->out, last_certain_backwards - 1, -1));

			assert(!have_bound_at(&ops->out, last_certain_forwards - 1, 1));
			if ((uncertainty & UNCERTAIN_FORWARDS) == 0) {
				size_t shift = pull_bound_check(ops, &this_op_pos, last_certain_forwards, curr_off_fwd, 1);
				end_pos += shift;
				if (last_certain_backwards > last_certain_forwards)
					last_certain_backwards += shift;
			} else {
				last_certain_forwards = end_pos;
				curr_off_fwd = 0;
			}
			assert(!have_bound_at(&ops->out, last_certain_forwards - 1, 1));
		} else if (op->op_type == BF_OP_SKIP) {
			if (op->offset > 0) {
				last_certain_forwards = end_pos;
				curr_off_fwd = 0;
			} else {
				last_certain_backwards = end_pos;
				curr_off_bck = 0;
			}
		}

		i = end_pos;
	}
	ops->out.ops = realloc(ops->out.ops, ops->out.len * sizeof *ops->out.ops);
}
