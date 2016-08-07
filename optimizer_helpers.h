#ifndef USING_OPTIMIZER_HELPERS_H
#define USING_OPTIMIZER_HELPERS_H

#include "brainfuck.h"

bool ensures_zero(bf_op const *op);
bool ensures_nonzero(bf_op const *op);
bool writes_cell(bf_op const *op);
bool moves_tape(bf_op *op);
bool expects_nonzero(bf_op *op);
loop_info get_loop_info(bf_op *op);

#endif
