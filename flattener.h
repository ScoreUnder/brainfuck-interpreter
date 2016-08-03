#ifndef USING_FLATTENER_H
#define USING_FLATTENER_H

#include <stdlib.h>
#include "brainfuck.h"

typedef struct {
	char *data;
	size_t pos, len;
} blob_cursor;

void flatten_bf(bf_op *ops, blob_cursor *out);

#endif
