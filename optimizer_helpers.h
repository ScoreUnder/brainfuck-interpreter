#ifndef USING_OPTIMIZER_HELPERS_H
#define USING_OPTIMIZER_HELPERS_H

#include "brainfuck.h"

// Loop balance constants
#define UNCERTAIN_FORWARDS  1
#define UNCERTAIN_BACKWARDS 2
#define UNCERTAIN_BOTH      3
#define UNCERTAINTY_PRESENT 4

bool ensures_zero(bf_op const *op);
bool ensures_nonzero(bf_op const *op);
bool writes_cell(bf_op const *op);
bool moves_tape(bf_op *op);
int get_loop_balance(bf_op *op);
bool loops_only_once(bf_op *op);

#endif
