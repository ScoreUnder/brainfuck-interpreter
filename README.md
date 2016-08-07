brainfuck-interpreter
=====================

An optimizing brainfuck interpreter, just like the rest.

Optimizer suggestions greatly welcomed!

Quirks
------

### EOF

EOF is zero if 8-bit cells are used, or -1 otherwise. (This is because
-1 is lossless in that case, so it doesn't make sense to me to use
anything else.)

### Tape size

Unless compiled with the `-DFIXED_TAPE_SIZE=uintXX_t` option, it will use
an infinite tape.

With `FIXED_TAPE_SIZE`, bounds checks are avoided entirely by relying on
the wrapping behaviour of the specified integer type.

Internals
---------

In short, the interpreter goes through these stages:

1. `parser.c`: Read and parse file (creates an AST)
2. `optimizer.c`: Optimize AST
3. `optimizer.c:add_bounds_checks`: Insert bound checking instructions into AST
4. `flattener.c`: Flatten AST into bytecode
5. `interpreter.c`: Execute bytecode

The `brainfuck.h` file contains various opcodes which are used
internally, and describes at which stages they are generated.
