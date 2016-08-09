#ifndef USING_OPTIMIZER_HELPERS_H
#define USING_OPTIMIZER_HELPERS_H

#include "brainfuck.h"

typedef struct {
	ssize_t pos;          // in: position to start checking from. out: position checking stopped at
	ssize_t offset;       // in: offset before pos is executed. out: offset after pos is executed
	bool read : 1;        // offset is definitely read
	bool write : 1;       // offset is definitely written to
	bool uncertain : 1;   // reached a point where nothing can be determined for sure
	bool maybe_read : 1;  // offset may be read depending on a loop's condition
	bool maybe_write : 1; // offset may be written depending on a loop's condition
} offset_access;

bool ensures_zero(bf_op const *op);
bool ensures_nonzero(bf_op const *op);
bool writes_cell(bf_op const *op);
bool moves_tape(bf_op *op);
bool performs_io(bf_op *op);
bool expects_nonzero(bf_op *op);
ssize_t get_final_offset(bf_op *op);
ssize_t get_max_offset(bf_op *op);
ssize_t get_min_offset(bf_op *op);
loop_info get_loop_info(bf_op *op);
offset_access offset_might_be_accessed(bf_op_builder *arr, offset_access initial);

#endif
