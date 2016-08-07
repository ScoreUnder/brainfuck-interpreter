#ifndef USING_FLATTENER_H
#define USING_FLATTENER_H

#include <stdlib.h>
#include "brainfuck.h"
#include "interpreter.h"

typedef struct {
	char *data;
	size_t pos, len;
} blob_cursor;

interpreter_meta flatten_bf(bf_op *ops, blob_cursor *out);

#endif
