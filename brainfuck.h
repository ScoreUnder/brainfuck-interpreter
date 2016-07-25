#ifndef USING_BRAINFUCK_H
#define USING_BRAINFUCK_H

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>

enum bf_op_type {
	                      // ┏━━━━ opcode is used in final Flattened representation
	                      // ┃┏━━━ opcode is generated by Optimizer
	                      // ┃┃┏━━ opcode is generated by Parser
	BF_OP_INVALID = 0,    // ┃┃┃┏━ opcode has no corresponding brainfuck structure (Unreal)
	BF_OP_ALTER,          // FOP  move the data pointer then modify the data underneath it
	BF_OP_IN,             // FOP  take a character on input, store at the data pointer
	BF_OP_OUT,            // FOP  output a character (specified at the data pointer)
	BF_OP_LOOP,           //  OP  a loop, corresponding to the [] operator
	BF_OP_ONCE,           // F  U a pseudo-op used to contain all other ops for execution
	BF_OP_BOUNDS_CHECK,   // FO U ensure the tape has enough space for several future operations
	BF_OP_SET,            // FO   set the absolute value of the data at the data pointer
	BF_OP_MULTIPLY,       // FO   multiply current data and store elsewhere
	BF_OP_SKIP,           // FO   skip cells until a zero is reached. optimization of loops like [>>>]
	BF_OP_ALTER_MOVEONLY, // F    move data pointer
	BF_OP_ALTER_ADDONLY,  // F    modify the data at the data pointer
	BF_OP_JUMPIFNONZERO,  // F    jump if nonzero (used to implement the loop)
	BF_OP_JUMPIFZERO,     // F    jump if zero (used to implement the loop)
	BF_OP_DIE,            // F  U a pseudo-op signalling the end of the program
};

typedef int8_t cell_int;

struct s_bf_op;

typedef struct s_bf_op_array {
	struct s_bf_op *ops;
	size_t len;
} bf_op_array;

typedef struct s_bf_op {
	enum bf_op_type op_type;
	int uncertainty;

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
bf_op* insert_bf_op(bf_op_builder *ops, size_t index);
void remove_bf_ops(bf_op_array *arr, size_t index, size_t count);

#endif
