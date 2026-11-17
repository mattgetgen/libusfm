# libusfm

## Command-line tool

`./build.sh release` builds `./usfm`, a small reference tool built on the
public API (`main.c`):

    ./usfm GEN.usfm              # colored USFM
    ./usfm -t GEN.usfm 1:3       # the AST of Genesis 1:3
    ./usfm -x books/ "JHN 3:16"  # plain text, from a directory
    ./usfm -b 100 books/         # parse speed

Run `./usfm -h` for every option.
