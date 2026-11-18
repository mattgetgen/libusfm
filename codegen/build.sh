#!/usr/bin/env bash

set -e

CC=gcc
CFLAGS=(
    -std=c99
    -Wall
    -Wextra
    -pedantic
    -O2
    -g
    -fsanitize=address
    -fsanitize=undefined
)

$CC "${CFLAGS[@]}" codegen.c -o codegen

