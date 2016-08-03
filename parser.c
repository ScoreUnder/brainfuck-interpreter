#include <stdbool.h>
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
	if (builder->len != 0) {
		bf_op *last = &builder->ops[builder->len - 1];
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

bf_op_builder build_bf_tree_internal(FILE *restrict input, bool stop_at_bang, bool expecting_bracket) {
	bf_op_builder builder;
	builder.alloc = 16;
	builder.len = 0;
	builder.ops = malloc(builder.alloc * sizeof *builder.ops);

	while (true) {
		int c = getc(input);
		if (c == EOF) {
			if (feof(input))
				break;
			else
				err(2, "could not read brainfuck code");
		}

		if (stop_at_bang && c == '!') {
			if (expecting_bracket)
				warnx("warning: found a bang inside a loop; ignoring.");
			else
				break;
		}

		if (op_type_for_char[c] == BF_OP_INVALID)
			continue;

		alloc_op_by_char(c, &builder);

		bf_op *op = &builder.ops[builder.len - 1];
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
				op->children = build_bf_tree_internal(input, stop_at_bang, true);
				optimize_loop(&builder);
				break;
			case ']':
				if (!expecting_bracket) errx(1, "Unexpected end of loop");
				goto end;
		}
	}

end:
	if (builder.len == 0) {
		free(builder.ops);
		builder.alloc = 0;
		builder.ops = NULL;
	}
	return builder;
}

bf_op build_bf_tree(FILE *input, bool stop_at_bang) {
	bf_op root = {.op_type = BF_OP_ONCE};
	root.children = build_bf_tree_internal(input, stop_at_bang, false);

#ifndef FIXED_TAPE_SIZE
	add_bounds_checks(&root.children);
#endif

	return root;
}
