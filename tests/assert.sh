#!/bin/sh

set -eu

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

pass() {
    printf 'PASS: %s\n' "$*"
}

assert_contains() {
    actual=$1
    expected=$2
    description=${3:-output contains expected text}

    printf '%s\n' "$actual" | grep -F -- "$expected" >/dev/null || \
        fail "$description (missing: $expected)"
}

assert_not_contains() {
    actual=$1
    unexpected=$2
    description=${3:-output does not contain unexpected text}

    if printf '%s\n' "$actual" | grep -F -- "$unexpected" >/dev/null; then
        fail "$description (found: $unexpected)"
    fi
}

assert_file_equals() {
    file=$1
    expected=$2
    description=${3:-file contents match expected text}

    [ -f "$file" ] || fail "$description (file does not exist: $file)"
    actual=$(cat "$file")
    [ "$actual" = "$expected" ] || {
        printf 'Expected:\n%s\nActual:\n%s\n' "$expected" "$actual" >&2
        fail "$description"
    }
}

strip_ansi() {
    sed 's/\033\[[0-9;]*[[:alpha:]]//g'
}