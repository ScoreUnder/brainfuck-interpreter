#ifndef USING_BRAINFUCK_H
#define USING_BRAINFUCK_H

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>

enum bf_op_type {
	BF_OP_INVALID = 0,
	BF_OP_ALTER, BF_OP_IN, BF_OP_OUT, BF_OP_LOOP,
	// Pseudo-ops
	BF_OP_ONCE,
	// Optimized ops
	BF_OP_SET, BF_OP_MULTIPLY, BF_OP_SKIP,
	// Flattened optimized ops
	BF_OP_ALTER_MOVEONLY, BF_OP_ALTER_ADDONLY,
	BF_OP_JUMP, BF_OP_JUMPIFZERO, BF_OP_DIE,
};

typedef int8_t cell_int;

struct s_bf_op;

typedef struct s_bf_op_array {
	struct s_bf_op *ops;
	size_t len;
} bf_op_array;

typedef struct s_bf_op {
	enum bf_op_type op_type;
#ifndef NDEBUG
	unsigned int count;
	uint64_t time;
#endif
	union {
		bf_op_array children;
		struct {
			ssize_t offset;
			cell_int amount;
		};
	};
} bf_op;

typedef struct {
	bf_op_array out;
	size_t alloc;
} bf_op_builder;

bf_op* alloc_bf_op(bf_op_builder *ops);
void remove_bf_ops(bf_op_array *arr, size_t index, size_t count);

#endif
