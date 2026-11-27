#!/usr/bin/env python3
"""Derive an untagged corpus from a word-tagged one.

Usage: python3 -I tests/corpus/derive.py INPUT_DIR OUTPUT_DIR

Copies every *.usfm file in INPUT_DIR to OUTPUT_DIR with each word-level
wrapper reduced to its text:

    \\w TEXT|ATTRS\\w*    ->  TEXT
    \\+w TEXT|ATTRS\\+w*  ->  TEXT

and copies copr.htm. Nothing else is changed (bytes are preserved).

Used for:
  WEB      from an unpacked WEB-STRONGS (drops the Strong's numbers)
  HBO-WLC  from an unpacked https://ebible.org/Scriptures/hboWLC_usfm.zip
           (drops the CC BY-SA lemma/morphology attributes, keeping only
           the public-domain text)
"""
import os
import re
import shutil
import sys

# \w or \+w, the word (no backslash, no bar), optional |attributes, and the
# matching closing marker.
WORD = re.compile(rb"\\(\+?)w ([^|\\]*)(?:\|[^\\]*)?\\\1w\*")
LEFTOVER = (rb"\w ", rb"\+w ", rb"\w*", rb"\+w*", b"strong=", b"x-morph=")


def strip(data):
    out, n = WORD.subn(rb"\2", data)
    for needle in LEFTOVER:
        if needle in out:
            raise SystemExit("leftover %r after stripping" % needle)
    return out, n


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    names = sorted(n for n in os.listdir(src) if n.endswith(".usfm"))
    if not names:
        raise SystemExit("no .usfm files in " + src)
    total = 0
    for name in names:
        with open(os.path.join(src, name), "rb") as f:
            data = f.read()
        out, n = strip(data)
        total += n
        with open(os.path.join(dst, name), "wb") as f:
            f.write(out)
    shutil.copyfile(os.path.join(src, "copr.htm"), os.path.join(dst, "copr.htm"))
    print("%s: %d files, %d word wrappers removed" % (dst, len(names), total))


if __name__ == "__main__":
    main()
