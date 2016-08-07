#!/bin/sh
build_release() { make -B CPPFLAGS="$2 -DNDEBUG" && mv brainfuck "$1"; }
build_debug() { make -B CFLAGS='-Og -ggdb -std=c11 -Wall -Wextra -pedantic' CPPFLAGS="$2" && mv brainfuck "$1"; }

build() {
    printf '> Building %s-release\n' "$1"
    build_release "$1-release" "$2"
    printf '> Building %s-debug\n' "$1"
    build_debug "$1-debug" "$2"
}

build brainfuck-8
build brainfuck-16 -DCELL_INT=int16_t
build brainfuck-32 -DCELL_INT=int32_t
build brainfuck-64 -DCELL_INT=int64_t
