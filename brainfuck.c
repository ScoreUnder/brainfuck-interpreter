/*
 * Hideously generic brainfuck AST module: functions which
 * allocate/deallocate parts of the tree.
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>

#include "brainfuck.h"

bf_op* alloc_bf_op(bf_op_builder *ops) {
	assert(ops != NULL);
	assert(ops->ops != NULL);
	assert(ops->alloc != 0);

	if (ops->len == ops->alloc) {
		ops->alloc *= 2;
		ops->ops = realloc(ops->ops, ops->alloc * sizeof *ops->ops);
	}
	return &ops->ops[ops->len++];
}

bf_op* insert_bf_ops(bf_op_builder *ops, size_t index, size_t count) {
	assert(ops != NULL);
	assert(ops->ops != NULL);
	assert(ops->alloc != 0);

	if (ops->len + count > ops->alloc) {
		while (ops->len + count > ops->alloc) {
			ops->alloc *= 2;
		}
		ops->ops = realloc(ops->ops, ops->alloc * sizeof *ops->ops);
	}

	memmove(ops->ops + index + count, ops->ops + index, (ops->len - index) * sizeof *ops->ops);
	ops->len += count;
	return &ops->ops[index];
}

void remove_bf_ops(bf_op_builder *arr, size_t index, size_t count) {
	assert(arr != NULL);
	assert(arr->ops != NULL);
	assert(index + count <= arr->len);

	for (size_t i = index; i < index + count; i++) {
		free_bf_op_children(&arr->ops[i]);
	}
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
