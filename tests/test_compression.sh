#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)
# shellcheck source=assert.sh
. "$TEST_DIR/assert.sh"

work_dir=$(mktemp -d "${TMPDIR:-/tmp}/sys-shell-compression.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cd "$work_dir"

# Keep the input large enough for the background worker to overlap with the REPL.
awk 'BEGIN {
    line = "";
    for (i = 0; i < 2048; i++) line = line "ABCD";
    for (i = 0; i < 4096; i++) print line;
}' > original.txt

input=$(printf '%s\n' \
    'editor original.txt' \
    'c original.thf' \
    'status' \
    'a save-must-be-rejected' \
    'w' \
    'wait' \
    'status' \
    'qw' \
    'editor restored.txt' \
    'u original.thf' \
    'status' \
    'a must-also-be-rejected' \
    'wait' \
    'p 1' \
    'qw' \
    'exit')

output=$(printf '%s\n' "$input" | "$ROOT_DIR/sys_shell" 2>&1 | strip_ansi)

assert_contains "$output" 'Compression started in background:' \
    'compression starts asynchronously'
assert_contains "$output" 'Background compressing:' \
    'status reports compression progress'
assert_contains "$output" 'Background compressing finished: success' \
    'wait joins the compression worker'
assert_contains "$output" 'Background task: idle' \
    'status reports idle after wait'
assert_contains "$output" 'editor is busy with a background compression task' \
    'editor mutation is rejected while compression runs'
assert_contains "$output" 'Decompression started in background' \
    'decompression starts asynchronously'
assert_contains "$output" 'Background decompressing:' \
    'status reports decompression progress'
assert_contains "$output" 'Background decompressing finished: success' \
    'wait joins the decompression worker'
assert_contains "$output" 'ABCDABCD' \
    'decompressed content is available in the editor'

[ -s original.thf ] || fail 'compression creates a non-empty output file'
cmp original.txt restored.txt || fail 'decompression restores the original bytes'
pass 'background compression, status, wait, race protection, and decompression'