#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
# shellcheck source=assert.sh
. "$TEST_DIR/assert.sh"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sys-shell-edge.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cd "$work_dir"

repeated_letters=
i=0
while [ "$i" -lt 600 ]; do
    repeated_letters=${repeated_letters}A
    i=$((i + 1))
done
printf '%s\n' "$repeated_letters" > long_line.txt

whitespace_line=$(printf 'left   \t\tmiddle        right')
printf '%s\n' "$whitespace_line" > whitespace.txt

many_lines=120
: > many_lines.txt
i=1
while [ "$i" -le "$many_lines" ]; do
    printf 'line-%03d\n' "$i" >> many_lines.txt
    i=$((i + 1))
done

input=$(printf '%s\n' \
    'editor whitespace.txt' \
    'p 1' \
    'qw' \
    'editor long_line.txt' \
    'p 1' \
    'qw' \
    'editor many_lines.txt' \
    'p 1' \
    'p 120' \
    'i 121 line-121' \
    'p 121' \
    'd 60' \
    'qw' \
    'exit')

output=$(printf '%s\n' "$input" | "$ROOT_DIR/sys_shell" 2>&1 | strip_ansi)

assert_contains "$output" "$whitespace_line" 'multiple spaces and tabs are preserved'
assert_contains "$output" "$repeated_letters" 'long repeated-character line is printed completely'
assert_contains "$output" 'line-001' 'first line of the generated document is readable'
assert_contains "$output" 'line-120' 'last generated line is readable'
assert_contains "$output" 'line-121' 'insertion after the last generated line succeeds'

assert_file_equals 'whitespace.txt' "$whitespace_line" 'whitespace-only formatting survives save'
assert_file_equals 'long_line.txt' "$repeated_letters" 'long repeated-character line survives save'

expected_many=$(printf 'line-001')
i=2
while [ "$i" -le "$many_lines" ]; do
    if [ "$i" -ne 60 ]; then
        expected_many=$(printf '%s\nline-%03d' "$expected_many" "$i")
    fi
    i=$((i + 1))
done
expected_many=$(printf '%s\nline-121' "$expected_many")
assert_file_equals 'many_lines.txt' "$expected_many" 'many-line delete and end insertion produce exact content'

pass 'whitespace, repeated characters, long lines, and many-line edge cases'