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
		default:
			return false;
	}
}

bool moves_tape(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_SKIP:
			return true;
		case BF_OP_ALTER:
			return op->offset != 0;
		case BF_OP_LOOP:
			return get_loop_balance(op) != 0;
		default:
			return false;
	}
}

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

	if (op->children.len == 0)
		return 0;

	assert(op->children.ops != NULL);

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

bool loops_only_once(bf_op *loop) {
	assert(loop->op_type == BF_OP_LOOP);

	if (loop->children.len == 0) return false;

	bf_op *last = &loop->children.ops[loop->children.len - 1];
	if (ensures_zero(last))
		return true;
	if (last->definitely_zero && !moves_tape(last) && !writes_cell(last))
		return true;

	return false;
}
