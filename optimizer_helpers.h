#ifndef USING_OPTIMIZER_HELPERS_H
#define USING_OPTIMIZER_HELPERS_H

#include "brainfuck.h"

bool ensures_zero(bf_op const *op);
bool ensures_nonzero(bf_op const *op);
bool writes_cell(bf_op const *op);
bool moves_tape(bf_op *op);
bool expects_nonzero(bf_op *op);
ssize_t get_final_offset(bf_op *op);
ssize_t get_max_offset(bf_op *op);
ssize_t get_min_offset(bf_op *op);
loop_info get_loop_info(bf_op *op);
bool offset_might_be_accessed(ssize_t offset, bf_op_builder *arr, size_t start, size_t end, bool include_reads, bool include_writes);

#endif
