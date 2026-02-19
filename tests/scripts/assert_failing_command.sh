#!/bin/sh

set -eu

if [ "$#" -lt 2 ]; then
  echo "usage: assert_failing_command.sh <expected-regex> <command> [args...]" >&2
  exit 2
fi

expected_regex="$1"
shift

set +e
output="$("$@" 2>&1)"
status=$?
set -e

printf '%s\n' "$output"

if [ "$status" -eq 0 ]; then
  echo "expected command to fail but it succeeded" >&2
  exit 1
fi

if ! printf '%s\n' "$output" | grep -E -- "$expected_regex" >/dev/null 2>&1; then
  echo "output did not match expected regex: $expected_regex" >&2
  exit 1
fi
