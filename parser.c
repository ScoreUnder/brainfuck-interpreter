#include <err.h>

#include "parser.h"
#include "optimizer.h"

static char op_type_for_char[256] = {
	['+'] = BF_OP_ALTER,
	['-'] = BF_OP_ALTER,
	['<'] = BF_OP_ALTER,
	['>'] = BF_OP_ALTER,
	[','] = BF_OP_IN,
	['.'] = BF_OP_OUT,
	['['] = BF_OP_LOOP,
	[']'] = BF_OP_LOOP,
};

static void alloc_op_by_char(unsigned char op_char, bf_op_builder *builder) {
	bool need_alloc = true;
	if (builder->out.len != 0) {
		bf_op *last = &builder->out.ops[builder->out.len - 1];
		switch (op_char) {
			case '+':
			case '-':
				if (last->op_type == BF_OP_ALTER || last->op_type == BF_OP_SET)
					need_alloc = false;
				break;
			case '>':
			case '<':
				if (last->op_type == BF_OP_ALTER && last->amount == 0)
					need_alloc = false;
				break;
		}
	}
	if (op_char == ']')
		need_alloc = false;
	if (need_alloc)
		*alloc_bf_op(builder) = (bf_op){
			.op_type = op_type_for_char[op_char],
		};
}

bf_op* build_bf_tree(blob_cursor *restrict input, bool expecting_bracket, size_t *out_len) {
	bf_op_builder builder;
	builder.alloc = 16;
	builder.out.len = 0;
	builder.out.ops = malloc(builder.alloc * sizeof *builder.out.ops);

	while (input->pos < input->len) {
		unsigned char c = input->data[input->pos++];
		if (op_type_for_char[c] == BF_OP_INVALID)
			continue;

		alloc_op_by_char(c, &builder);

		bf_op *op = &builder.out.ops[builder.out.len - 1];
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
				op->children.ops = build_bf_tree(input, true, &op->children.len);
				optimize_loop(&builder);
				break;
			case ']':
				if (!expecting_bracket) errx(1, "Unexpected end of loop");
				goto end;
		}
	}

end:
	*out_len = builder.out.len;
	if (builder.out.len == 0) {
		free(builder.out.ops);
		builder.out.ops = NULL;
	} else {
		builder.out.ops = realloc(builder.out.ops, builder.out.len * sizeof *builder.out.ops);
	}
	return builder.out.ops;
}
