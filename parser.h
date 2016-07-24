#ifndef USING_PARSER_H
#define USING_PARSER_H

#include <stdbool.h>

#include "brainfuck.h"

typedef struct {
	char *data;
	size_t pos, len;
} blob_cursor;

bf_op* build_bf_tree(blob_cursor *input, bool expecting_bracket, size_t *out_len);

#endif
