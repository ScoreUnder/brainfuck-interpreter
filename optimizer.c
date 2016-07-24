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
	// Shite optimizations
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

void add_bounds_checks(bf_op_builder *ops) {
	assert(ops != NULL);
	if (ops->out.len == 0)
		return;

	assert(ops->out.ops != NULL);

	for (size_t i = 0; i < ops->out.len;) {
		ssize_t max_bound = 0, min_bound = 0, current_offset = 0;
		size_t end_pos = i;
		while (end_pos < ops->out.len) {
			bf_op *op = &ops->out.ops[end_pos];
			if (op->op_type == BF_OP_LOOP || op->op_type == BF_OP_SKIP)
				break;

			if (op->op_type == BF_OP_ALTER || op->op_type == BF_OP_MULTIPLY)
				current_offset += op->offset;

			if (current_offset < min_bound)
				min_bound = current_offset;
			else if (current_offset > max_bound)
				max_bound = current_offset;

			end_pos++;
		}

		if (max_bound) {
			*insert_bf_op(ops, i) = (bf_op) {
				.op_type = BF_OP_BOUNDS_CHECK,
				.offset = max_bound,
			};
			end_pos++;
		}
		if (min_bound) {
			*insert_bf_op(ops, i) = (bf_op) {
				.op_type = BF_OP_BOUNDS_CHECK,
				.offset = min_bound,
			};
			end_pos++;
		}

		i = end_pos;

		assert(end_pos <= ops->out.len);
		if (end_pos >= ops->out.len)
			break;

		bf_op *op = &ops->out.ops[i++];
		if (op->op_type == BF_OP_LOOP) {
			// Playing with fire...
			// Wrap op children in a reallocatable structure
			bf_op_builder builder = {
				.out = op->children,
				.alloc = op->children.len,
			};
			add_bounds_checks(&builder);
			op->children = builder.out;
		}
	}
	ops->out.ops = realloc(ops->out.ops, ops->out.len * sizeof *ops->out.ops);
}
