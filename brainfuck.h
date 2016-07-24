#ifndef USING_BRAINFUCK_H
#define USING_BRAINFUCK_H

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>

enum {
	BF_OP_ALTER, BF_OP_IN, BF_OP_OUT, BF_OP_LOOP,
	// Pseudo-ops
	BF_OP_ONCE,
	// Optimized ops
	BF_OP_SET, BF_OP_MULTIPLY, BF_OP_SKIP,
};

typedef int8_t cell_int;

typedef struct s_bf_op {
	unsigned int op_type;
#ifndef NDEBUG
	unsigned int count;
	uint64_t time;
#endif
	union {
		struct {
			size_t child_op_count;
			struct s_bf_op *child_op;
		};
		struct {
			ssize_t offset;
			cell_int amount;
		};
	};
} bf_op;

#endif
