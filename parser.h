#ifndef USING_PARSER_H
#define USING_PARSER_H

#include <stdbool.h>

#include "brainfuck.h"

bf_op* build_bf_tree(char *data, size_t *pos, size_t len, bool expecting_bracket, size_t *out_len);

#endif
