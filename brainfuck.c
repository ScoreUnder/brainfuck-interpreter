/*
 * Hideously generic brainfuck AST module: functions which
 * allocate/deallocate parts of the tree.
 */
#include <assert.h>
#include <string.h>

#include "brainfuck.h"

bf_op* alloc_bf_op(bf_op_builder *ops) {
	assert(ops != NULL);
	assert(ops->out.ops != NULL);
	assert(ops->alloc != 0);

	if (ops->out.len == ops->alloc) {
		ops->alloc *= 2;
		ops->out.ops = realloc(ops->out.ops, ops->alloc * sizeof *ops->out.ops);
	}
	return &ops->out.ops[ops->out.len++];
}

bf_op* insert_bf_op(bf_op_builder *ops, size_t index) {
	assert(ops != NULL);
	assert(ops->out.ops != NULL);
	assert(ops->alloc != 0);

	if (ops->out.len == ops->alloc) {
		ops->alloc *= 2;
		ops->out.ops = realloc(ops->out.ops, ops->alloc * sizeof *ops->out.ops);
	}
	memmove(ops->out.ops + index + 1, ops->out.ops + index, (ops->out.len - index) * sizeof *ops->out.ops);
	ops->out.len++;
	return &ops->out.ops[index];
}

void remove_bf_ops(bf_op_array *arr, size_t index, size_t count) {
	assert(arr != NULL);
	assert(arr->ops != NULL);
	assert(index + count <= arr->len);

	memmove(arr->ops + index, arr->ops + index + count,
			(arr->len - index - count) * sizeof *arr->ops);
	arr->len -= count;
}

void free_bf_op_children(bf_op *op) {
	switch (op->op_type) {
		case BF_OP_LOOP:
		case BF_OP_ONCE: {
			size_t children = op->children.len;
			for (size_t i = 0; i < children; i++)
				free_bf_op_children(&op->children.ops[i]);
			if (children != 0)
				free(op->children.ops);
			break;
		}

		default:
			// Everything else is fine
			break;
	}
}
