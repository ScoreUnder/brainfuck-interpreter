#ifndef USING_OPTIMIZER_H
#define USING_OPTIMIZER_H

#include "brainfuck.h"
#include "parser.h"

void optimize_root(bf_op_builder *ops);
void optimize_loop(bf_op_builder *ops);
void add_bounds_checks(bf_op_builder *ops);

#endif
