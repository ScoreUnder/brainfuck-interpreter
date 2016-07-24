#include <err.h>

#include "parser.h"
#include "optimizer.h"

static char op_type_for_char[] = {
	['+'] = BF_OP_ALTER,
	['-'] = BF_OP_ALTER,
	['<'] = BF_OP_ALTER,
	['>'] = BF_OP_ALTER,
	[','] = BF_OP_IN,
	['.'] = BF_OP_OUT,
	['['] = BF_OP_LOOP,
};

bf_op* alloc_bf_op(bf_op_builder *ops) {
	if (ops->len == ops->alloc) {
		ops->alloc *= 2;
		ops->out = realloc(ops->out, ops->alloc * sizeof *ops->out);
	}
	return &ops->out[ops->len++];
}

bf_op* build_bf_tree(blob_cursor *restrict input, bool expecting_bracket, size_t *out_len) {
	bf_op_builder ops;
	ops.alloc = 16;
	ops.len = 0;
	ops.out = malloc(ops.alloc * sizeof *ops.out);

	while (input->pos < input->len) {
		unsigned char c = input->data[input->pos++];
		switch (c) {
			case '+':
			case '-':
				if (ops.len == 0
						|| (ops.out[ops.len - 1].op_type != BF_OP_ALTER
							&& ops.out[ops.len - 1].op_type != BF_OP_SET)) {
new_alter:
					*alloc_bf_op(&ops) = (bf_op){
						.op_type = BF_OP_ALTER,
						.offset = 0,
						.amount = 0,
					};
				}
				break;
			case '>':
			case '<':
				if (ops.len == 0
						|| ops.out[ops.len - 1].op_type != BF_OP_ALTER
						|| ops.out[ops.len - 1].amount != 0) {
					goto new_alter;
				}
				break;
			case '.':
			case ',':
			case '[':
				*alloc_bf_op(&ops) = (bf_op){
					.op_type = op_type_for_char[c],
				};
				break;
		}

		bf_op *op = &ops.out[ops.len - 1];
		switch (c) {
			case '+':
				op->amount++;
				break;
			case '-':
				op->amount--;
				break;
			case '>':
				op->offset++;
				break;
			case '<':
				op->offset--;
				break;
			case '[':
				op->child_op = build_bf_tree(input, true, &op->child_op_count);
				optimize_loop(&ops);
				break;
			case ']':
				if (!expecting_bracket) errx(1, "Unexpected end of loop");
				goto end;
		}
	}

end:
	*out_len = ops.len;
	if (ops.len == 0) {
		free(ops.out);
		ops.out = NULL;
	} else {
		ops.out = realloc(ops.out, ops.len * sizeof *ops.out);
	}
	return ops.out;
}
