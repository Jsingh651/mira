#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/mirac"
PASS=0
FAIL=0

if [[ ! -x "$BIN" ]]; then
    echo "Building mirac..."
    make -C "$ROOT"
fi

run_runtime_test() {
    local name="$1"
    local mira="tests/${name}.mira"
    local expected="$ROOT/tests/${name}.expected"
    local out
    out="$(mktemp)"
    if (cd "$ROOT" && "$BIN" "$mira" >"$out" 2>/dev/null); then
        if diff -u "$expected" "$out" >/dev/null; then
            echo "PASS  $name"
            PASS=$((PASS + 1))
        else
            echo "FAIL  $name (stdout mismatch)"
            diff -u "$expected" "$out" || true
            FAIL=$((FAIL + 1))
        fi
    else
        echo "FAIL  $name (expected success, got error)"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$out"
}

run_error_test() {
    local name="$1"
    local mira="tests/${name}.mira"
    local expected="$ROOT/tests/${name}.expected"
    local err
    err="$(mktemp)"
    if (cd "$ROOT" && "$BIN" "$mira" >/dev/null 2>"$err"); then
        echo "FAIL  $name (expected failure, succeeded)"
        FAIL=$((FAIL + 1))
    elif diff -u "$expected" "$err" >/dev/null; then
        echo "PASS  $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL  $name (stderr mismatch)"
        diff -u "$expected" "$err" || true
        FAIL=$((FAIL + 1))
    fi
    rm -f "$err"
}

run_disasm_test() {
    local name="$1"
    local mira="tests/${name}.mira"
    local expected="$ROOT/tests/${name}.disasm.expected"
    local out
    out="$(mktemp)"
    if (cd "$ROOT" && "$BIN" --disasm "$mira" >"$out" 2>/dev/null); then
        if diff -u "$expected" "$out" >/dev/null; then
            echo "PASS  $name (disasm)"
            PASS=$((PASS + 1))
        else
            echo "FAIL  $name (disasm mismatch)"
            diff -u "$expected" "$out" || true
            FAIL=$((FAIL + 1))
        fi
    else
        echo "FAIL  $name (disasm failed)"
        FAIL=$((FAIL + 1))
    fi
    rm -f "$out"
}

# Runtime tests
for name in fib arithmetic strings logic if_else while_loop factorial shadow fold dce; do
    run_runtime_test "$name"
done

# Disassembly tests (optimizer verification)
for name in fold dce; do
    run_disasm_test "$name"
done

# Error tests
for name in lex_error parse_error undeclared type_mismatch type_condition type_args type_return_path div_zero; do
    run_error_test "$name"
done

echo ""
echo "$PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
