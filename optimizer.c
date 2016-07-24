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
	assert(op->child_op_count == 0 || op->child_op != NULL);

	for (size_t i = 0; i < op->child_op_count; i++)
		if (op->child_op[i].op_type != BF_OP_ALTER)
			return false;

	return true;
}

void make_offsets_absolute(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->child_op_count == 0 || op->child_op != NULL);

	ssize_t current_offset = 0;
	for (size_t i = 0; i < op->child_op_count; i++) {
		assert(op->child_op[i].op_type == BF_OP_ALTER);

		current_offset += op->child_op[i].offset;
		op->child_op[i].offset = current_offset;
	}
}

void make_offsets_relative(bf_op *restrict op) {
	assert(op != NULL);
	assert(op->op_type == BF_OP_LOOP);
	assert(op->child_op_count == 0 || op->child_op != NULL);

	ssize_t current_offset = 0;
	for (size_t i = 0; i < op->child_op_count; i++) {
		assert(op->child_op[i].op_type == BF_OP_ALTER);

		op->child_op[i].offset -= current_offset;
		current_offset += op->child_op[i].offset;
	}
}

void remove_bf_ops(bf_op_builder *ops, size_t index, size_t count) {
	memmove(ops->out + index, ops->out + index + count,
			(ops->len - index - count) * sizeof *ops->out);
	ops->len -= count;
}

void remove_child_ops(bf_op *op, size_t index, size_t count) {
	memmove(op->child_op + index, op->child_op + index + count,
			(op->child_op_count - index - count) * sizeof *op->child_op);
	op->child_op_count -= count;
}

bool make_loop_into_multiply(bf_op_builder *ops) {
	assert(ops != NULL);
	assert(ops->out != NULL);
	assert(ops->len > 0);

	size_t my_op_index = ops->len - 1;
	bf_op *op = &ops->out[my_op_index];

	assert(op->op_type == BF_OP_LOOP);

	if (!is_loop_alter_only(op)) return false;

	make_offsets_absolute(op);

	if (op->child_op[op->child_op_count - 1].offset != 0) {
		// "unbalanced" loop, don't change it
error:
		make_offsets_relative(op);
		return false;
	}

	// ensure the loop variable decreases by one each time
	cell_int loop_increment = 0;
	for (size_t i = 0; i < op->child_op_count; i++)
		if (op->child_op[i].offset == 0)
			loop_increment += op->child_op[i].amount;

	if (loop_increment != (cell_int)-1) goto error;

	// Go along finding and removing all alter ops with the same offset
	while (op->child_op_count) {
		ssize_t target_offset = op->child_op[0].offset;
		cell_int final_amount = op->child_op[0].amount;

		remove_child_ops(op, 0, 1);
		for (size_t i = 0; i < op->child_op_count; i++) {
			if (op->child_op[i].offset != target_offset)
				continue;

			final_amount += op->child_op[i].amount;
			remove_child_ops(op, i--, 1);
		}

		if (target_offset != 0) {
			*alloc_bf_op(ops) = (bf_op) {
				.op_type = BF_OP_MULTIPLY,
				.offset = target_offset,
				.amount = final_amount,
			};
		}
	}

	free(op->child_op);
	remove_bf_ops(ops, my_op_index, 1);

	*alloc_bf_op(ops) = (bf_op) {
		.op_type = BF_OP_SET,
		.offset = 0,
		.amount = 0,
	};
	return true;
}

void optimize_loop(bf_op_builder *ops) {
	bf_op *op = &ops->out[ops->len - 1];
	// Shite optimizations
	if (op->child_op_count == 1
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[0].amount == 0) {
		ssize_t offset = op->child_op[0].offset;
		free(op->child_op);
		op->op_type = BF_OP_SKIP;
		op->offset = offset;
	} else if (op->child_op_count == 2
			&& op->child_op[0].op_type == BF_OP_ALTER
			&& op->child_op[1].op_type == BF_OP_ALTER
			&& op->child_op[0].offset == -op->child_op[1].offset
			&& op->child_op[1].amount == (cell_int)-1) {
		ssize_t offset = op->child_op[0].offset;
		cell_int scalar = op->child_op[0].amount;
		free(op->child_op);
		op->op_type = BF_OP_MULTIPLY;
		op->offset = offset;
		op->amount = scalar;
	} else if (make_loop_into_multiply(ops)) {
		return;
	}
}
