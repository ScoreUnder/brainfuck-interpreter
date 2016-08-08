#!/bin/sh
build_opt() { make -B CPPFLAGS="$1 -DNDEBUG"; }
build_debug() { make -B CFLAGS='-Og -ggdb -std=c11 -Wall -Wextra -pedantic' CPPFLAGS="$1"; }

build() {
    name=$1
    extra_cppflags=$2

    printf '> Building %s\n' "brainfuck-$name"
    build_opt "$extra_cppflags"
    for f in 'brainfuck' 'brainfuck2c'; do
        mv -- "$f" "$f-$name"
    done

    name=$1-debug
    printf '> Building %s\n' "brainfuck-$name"
    build_debug "$extra_cppflags"
    for f in 'brainfuck' 'brainfuck2c'; do
        mv -- "$f" "$f-$name"
    done
}

echo_and_run() {
    printf '%s ' "$@"
    printf \\n
    "$@"
}

clean() {
    set --
    for bits in 8 16 32 64; do
        for type in '' '-debug'; do
            for executable in brainfuck- brainfuck2c-; do
                set -- "$@" "$executable$bits$type"
            done
        done
    done
    echo_and_run rm -f -- "$@"
}

case $1 in
    (clean)
        make clean
        clean
        ;;
    (''|all)
        for bits in 8 16 32 64; do
            build "$bits" -DCELL_INT="int${bits}_t"
        done
        ;;
    (*)
        printf 'Unknown argument %s\n' "$1" >&2
        exit 1
        ;;
esac
