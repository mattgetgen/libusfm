#!/usr/bin/env bash
# Tests for the usfm command-line tool. Usage: tests/cli_test.sh USFM_BINARY [CORPUS_DIR]
# Set UPDATE=1 to rewrite the golden files (review the diff before committing).
set -u
BIN="$1"
CORPUS="${2:-}"
DIR="$(cd "$(dirname "$0")" && pwd)/cli"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
FAILED=0
PASSED=0

fail() { echo "cli: FAIL: $*" >&2; FAILED=$((FAILED + 1)); }
pass() { PASSED=$((PASSED + 1)); }

# golden NAME ARGS...: stdout of `usfm -M ARGS` must equal cli/expected/NAME.
golden() {
    local name="$1"; shift
    "$BIN" -M "$@" > "$TMP/$name" 2> "$TMP/$name.err"
    if [[ "${UPDATE:-0}" == 1 ]]; then
        cp "$TMP/$name" "$DIR/expected/$name"
    fi
    if diff -u "$DIR/expected/$name" "$TMP/$name" > "$TMP/$name.diff"; then pass
    else fail "$name"; cat "$TMP/$name.diff" >&2; fi
}

# status WANT ARGS...: exit status of `usfm ARGS` must be WANT.
status() {
    local want="$1"; shift
    "$BIN" "$@" > /dev/null 2>&1
    local got=$?
    if [[ "$got" == "$want" ]]; then pass; else fail "exit $got (want $want): $*"; fi
}

S="$DIR/sample.usfm"
mkdir -p "$DIR/expected"
golden usfm.txt -u "$S"
golden tree.txt -t "$S"
golden text.txt -x "$S"
for ref in 1 1:3 1:3-5 1:4 1:3-2:2 2:2; do
    name="${ref//:/.}"                  # no ':' in file names (Windows)
    golden "usfm-$name.txt" -u "$S" "$ref"
    golden "text-$name.txt" -x "$S" "$ref"
done
golden tree-2.2.txt -t "$S" 2:2
golden book-ref.txt "$S" "gen 1:1"

# The USFM view of the sample is the sample, and is stable.
if cmp -s "$S" "$DIR/expected/usfm.txt"; then pass; else fail "usfm view differs from the sample"; fi

status 0 "$S"
status 1 "$S" 9:9
status 1 "$S" "EXO 1:1"
status 2 "$S" 1:5-1:3
status 2 "$S" 1:1 1:2
status 2 "$TMP/missing.usfm"
status 2 -q "$S"
status 0 -h

# stdin gives the same output as the path.
if "$BIN" -M < "$S" | cmp -s - "$DIR/expected/usfm.txt"; then pass; else fail "stdin"; fi
if "$BIN" -M - < "$S" | cmp -s - "$DIR/expected/usfm.txt"; then pass; else fail "stdin as -"; fi

# A directory: its .usfm/.sfm files in name order, nothing else.
mkdir -p "$TMP/dir/sub.usfm"
cp "$S" "$TMP/dir/b.usfm"; cp "$S" "$TMP/dir/A.SFM"; echo junk > "$TMP/dir/c.txt"
"$BIN" -M "$TMP/dir" > "$TMP/dir.out" 2> "$TMP/dir.err"
cat "$DIR/expected/usfm.txt" "$DIR/expected/usfm.txt" > "$TMP/dir.want"
if cmp -s "$TMP/dir.out" "$TMP/dir.want"; then pass; else fail "directory output"; fi
if [[ "$(grep -c '^==> ' "$TMP/dir.err")" == 2 ]] && grep -q 'A.SFM <==' "$TMP/dir.err" &&
   [[ "$(grep -n 'A.SFM' "$TMP/dir.err" | cut -d: -f1)" -lt "$(grep -n 'b.usfm' "$TMP/dir.err" | cut -d: -f1)" ]]; then pass
else fail "directory headers"; cat "$TMP/dir.err" >&2; fi

# Color: on with -C (even with NO_COLOR), off in a file by default and with -M.
has_escape() { python3 -c 'import sys; sys.exit(0 if b"\x1b[" in open(sys.argv[1],"rb").read() else 1)' "$1"; }
"$BIN" -C "$S" > "$TMP/c1"; if has_escape "$TMP/c1"; then pass; else fail "-C has no color"; fi
NO_COLOR=1 "$BIN" -C "$S" > "$TMP/c2"; if has_escape "$TMP/c2"; then pass; else fail "-C under NO_COLOR has no color"; fi
"$BIN" "$S" > "$TMP/c3"; if has_escape "$TMP/c3"; then fail "color in a file"; else pass; fi
"$BIN" -C -M "$S" > "$TMP/c4"; if has_escape "$TMP/c4"; then fail "-C -M has color"; else pass; fi

# Diagnostics go to stderr with PATH:LINE:COL.
printf '\\id GEN\n\\c 1\n\\v 1 a \\w b\n' > "$TMP/diag.usfm"
"$BIN" -M -d "$TMP/diag.usfm" > /dev/null 2> "$TMP/diag.err"
if grep -q "diag.usfm:3:1: warning: implied paragraph" "$TMP/diag.err" &&
   grep -q "diag.usfm:3:8: error: unclosed marker" "$TMP/diag.err"; then pass
else fail "diagnostics"; cat "$TMP/diag.err" >&2; fi
# An implied paragraph writes no marker.
if [[ "$("$BIN" -M "$TMP/diag.usfm")" == $'\\id GEN\n\\c 1\n\\v 1 a \\w b' ]]; then pass
else fail "implied paragraph"; fi

# Legacy 8-bit input: an invalid UTF-8 diagnostic, still exit 0.
printf '\\id GEN\n\\c 1\n\\p\n\\v 1 caf\xe9\n' > "$TMP/cp1252.usfm"
if "$BIN" -M -d "$TMP/cp1252.usfm" > /dev/null 2> "$TMP/cp.err" &&
   grep -q ":4:9: error: invalid UTF-8" "$TMP/cp.err"; then pass
else fail "invalid UTF-8 diagnostic"; cat "$TMP/cp.err" >&2; fi
# -t does not print raw invalid bytes.
"$BIN" -M -t "$TMP/cp1252.usfm" > "$TMP/cp.tree" 2> /dev/null
if grep -q 'caf\\xE9' "$TMP/cp.tree" && ! grep -qa $'\xe9' "$TMP/cp.tree"; then pass
else fail "tree escapes invalid bytes"; fi
# UTF-16 input is refused with exit 2.
printf '\xff\xfe\\\0i\0d\0' > "$TMP/utf16.usfm"
"$BIN" -M "$TMP/utf16.usfm" > /dev/null 2> "$TMP/u16.err"; rc=$?
if [[ $rc == 2 ]] && grep -q "input is UTF-16 or UTF-32, not UTF-8" "$TMP/u16.err"; then pass
else fail "UTF-16 input (exit $rc)"; cat "$TMP/u16.err" >&2; fi

# Benchmark.
if "$BIN" -b 2 "$S" | grep -q 'MiB/s'; then pass; else fail "bench"; fi

# Every corpus file in every view, under the sanitizers of this build.
if [[ -n "$CORPUS" ]]; then
    for view in -u -t -x; do
        if "$BIN" -M "$view" "$CORPUS" > /dev/null 2> "$TMP/corpus.err"; then pass
        else fail "corpus $view"; tail -n 5 "$TMP/corpus.err" >&2; fi
    done
    # The USFM view is stable: printing its own output changes nothing.
    for f in "$CORPUS"/*.usfm; do
        "$BIN" -M "$f" > "$TMP/once"
        "$BIN" -M "$TMP/once" > "$TMP/twice"
        if ! cmp -s "$TMP/once" "$TMP/twice"; then fail "usfm view not stable: $f"; break; fi
    done
    pass
fi

echo "cli: $PASSED passed, $FAILED failed"
[[ "$FAILED" == 0 ]]
