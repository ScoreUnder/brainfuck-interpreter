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

void remove_bf_ops(bf_op_array *arr, size_t index, size_t count) {
	assert(arr != NULL);
	assert(arr->ops != NULL);
	assert(index + count <= arr->len);

	memmove(arr->ops + index, arr->ops + index + count,
			(arr->len - index - count) * sizeof *arr->ops);
	arr->len -= count;
}
