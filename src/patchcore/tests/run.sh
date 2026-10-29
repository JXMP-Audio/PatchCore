#!/usr/bin/env bash
# Builds and runs the PatchCore pure-logic tests (no Rack needed).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${TMPDIR:-/tmp}/jxmp-tests"
mkdir -p "$out"
for t in "$here"/test_*.cpp; do
	name="$(basename "$t" .cpp)"
	g++ -std=c++11 -Wall -Wextra -Werror -I"$here/.." "$t" -o "$out/$name"
	"$out/$name"
done
