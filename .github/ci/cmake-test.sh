#!/bin/sh
# ----------------------------------------------------------------------------
# Configure, build, and test a CMake configuration (used by `.github/workflows/test.yaml`).
# Usage: `sh .github/ci/cmake-test.sh <dir> <build-type> [cmake-args ...]`
#
# This configures `out/<dir>` with `-DCMAKE_BUILD_TYPE=<build-type> <cmake-args>`, builds it, and runs `ctest`.
#
# Environment:
#   CI_CMAKE          configure command (`cmake`), e.g. `emcmake cmake`
#   CI_CMAKE_ARGS     extra arguments for the configure
#   CI_CTEST_ARGS     extra arguments for the test run, e.g. `-E test-fork`
#   CI_CTEST_TIMEOUT  test timeout in seconds (240)
#   CI_PARALLEL       build parallelism (4)
# ----------------------------------------------------------------------------
set -e
if [ $# -lt 2 ]; then
  echo "usage: $0 <dir> <build-type> [cmake-args ...]"
  exit 1
fi
dir="out/$1"
type="$2"
shift 2
set -x
${CI_CMAKE:-cmake} . -B "$dir" -DCMAKE_BUILD_TYPE="$type" "$@" $CI_CMAKE_ARGS
cmake --build "$dir" --parallel "${CI_PARALLEL:-4}" --config "$type"
ctest --test-dir "$dir" --verbose --timeout "${CI_CTEST_TIMEOUT:-240}" -C "$type" $CI_CTEST_ARGS
