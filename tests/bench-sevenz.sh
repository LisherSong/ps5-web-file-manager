#!/usr/bin/env bash
# Thin wrapper -- the actual driver is tests/bench_driver.py.
#
#   /usr/bin/bash tests/bench-sevenz.sh [--big] [--runs N]
#
# bash is deliberately not used for timing here. In this sandbox every `date`
# costs ~350 ms, so a t0/t1 pair injects ~700 ms of overhead into a
# measurement whose real value is ~600 ms, and `time`'s user/sys accounting
# does not see into the native child at all (it reported 31 ms of CPU for a
# run that demonstrably decodes 82 MiB). Python reads a monotonic clock
# around a single spawn per sample instead.

set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd -W 2>/dev/null || pwd)"
exec python "$ROOT/tests/bench_driver.py" "$@"
