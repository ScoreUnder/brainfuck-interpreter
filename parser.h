#ifndef USING_PARSER_H
#define USING_PARSER_H

#include <stdbool.h>

#include "brainfuck.h"

typedef struct {
	bf_op *out;
	size_t len;
	size_t alloc;
} bf_op_builder;

typedef struct {
	char *data;
	size_t pos, len;
} blob_cursor;

bf_op* alloc_bf_op(bf_op_builder *ops); // TODO: Should I really have this in this file?
bf_op* build_bf_tree(blob_cursor *input, bool expecting_bracket, size_t *out_len);

#endif
