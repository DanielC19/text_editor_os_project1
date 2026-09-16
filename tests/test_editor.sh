#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
# shellcheck source=assert.sh
. "$TEST_DIR/assert.sh"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sys-shell-editor.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cd "$work_dir"
printf '%s\n' alpha beta gamma > editor.txt

input=$(printf '%s\n' \
    'editor editor.txt' \
    'p' \
    'p 0' \
    'p 99' \
    'i 4 omega' \
    'a final line' \
    's beta' \
    's absent' \
    'y 2' \
    'x 1' \
    'y 99' \
    'x 99' \
    'm' \
    'qw' \
    'd_read editor.txt' \
    'editor editor.txt' \
    'd 1' \
    'qd' \
    'd_read editor.txt' \
    'exit')

output=$(printf '%s\n' "$input" | "$ROOT_DIR/sys_shell" 2>&1 | strip_ansi)

assert_contains "$output" '1' 'p prints a line number'
assert_contains "$output" 'alpha' 'p prints the first line content'
assert_contains "$output" 'Error: invalid line number: 0' 'zero is rejected as a line number'
assert_contains "$output" 'Error: line 99 out of range' 'out-of-range print is rejected'
assert_contains "$output" 'Found '\''beta'\'' in line 2' 'search reports matching line'
assert_contains "$output" "Word 'absent' not found." 'search reports no match'
assert_contains "$output" 'Line 2 copied to clipboard.' 'copy reports success'
assert_contains "$output" 'Line pasted at position 1.' 'paste reports success'
assert_contains "$output" 'Error: line 99 out of range' 'out-of-range copy/paste is rejected'
assert_contains "$output" 'Unsaved changes: YES' 'metadata detects unsaved edits'
assert_contains "$output" "File 'editor.txt' saved successfully." 'qw saves the document'

expected_saved=$(printf '%s\n' beta alpha beta gamma omega 'final line')
assert_file_equals 'editor.txt' "$expected_saved" 'qw persists editor mutations'

assert_contains "$output" "$expected_saved" 'd_read shows the saved editor content'
pass 'editor crud, clipboard, metadata, save, and discard edge cases'