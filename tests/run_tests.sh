#!/bin/sh

set -eu

TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$TEST_DIR/.." && pwd)

printf 'Building sys_shell...\n'
make -C "$ROOT_DIR" sys_shell </dev/null >/dev/null

printf 'Running basic editor tests...\n'
"$TEST_DIR/test_editor.sh"
printf 'Running edge-case tests...\n'
"$TEST_DIR/test_edge_cases.sh"
printf 'All tests passed.\n'