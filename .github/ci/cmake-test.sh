#!/bin/sh
# ----------------------------------------------------------------------------
# Configure, build, and test a list of CMake configurations (used by `.github/workflows/test.yaml`).
# Usage: `sh .github/ci/cmake-test.sh < configs`, where each line of the input is a configuration:
#
#   [NAME=value ...] <dir> <build-type> [cmake-args ...]
#
# This configures `out/<dir>` with `-DCMAKE_BUILD_TYPE=<build-type> <cmake-args>`, builds it, and runs `ctest`.
# The leading `NAME=value` assignments are exported for that configuration only (e.g. `CC=clang` or
# `MIMALLOC_GUARDED_SAMPLE_RATE=1024`). Each line is expanded by the shell, so `$VAR` and quotes can be used.
# Empty lines and `#` comments are ignored.
#
# Environment:
#   CI_CMAKE          configure command (`cmake`), e.g. `emcmake cmake`
#   CI_CMAKE_ARGS     extra arguments for each configure
#   CI_CTEST_ARGS     extra arguments for each test run, e.g. `-E test-fork`
#   CI_CTEST_TIMEOUT  test timeout in seconds (240)
#   CI_PARALLEL       build parallelism (4)
#
# All configurations are run, and the script fails at the end if any of them failed.
# ----------------------------------------------------------------------------

run_config() {
  while [ $# -gt 0 ]; do
    case "$1" in
      [A-Za-z_]*=*) export "$1"; shift;;
      *) break;;
    esac
  done
  if [ $# -lt 2 ]; then
    echo "expecting a build directory and build type"
    return 1
  fi
  dir="out/$1"
  type="$2"
  shift 2
  set -x
  ${CI_CMAKE:-cmake} . -B "$dir" -DCMAKE_BUILD_TYPE="$type" "$@" $CI_CMAKE_ARGS &&
  cmake --build "$dir" --parallel "${CI_PARALLEL:-4}" --config "$type" &&
  ctest --test-dir "$dir" --verbose --timeout "${CI_CTEST_TIMEOUT:-240}" -C "$type" $CI_CTEST_ARGS
}

failed=""
while IFS= read -r line || [ -n "$line" ]; do
  if ! eval "set -- $line"; then
    echo "::error::invalid configuration: $line"
    failed="$failed
  $line"
    continue
  fi
  if [ $# -eq 0 ]; then continue; fi
  echo "::group::$*"
  ( run_config "$@" ) < /dev/null
  result=$?
  echo "::endgroup::"
  if [ $result -ne 0 ]; then
    echo "::error::failed: $*"
    failed="$failed
  $*"
  fi
done

if [ -n "$failed" ]; then
  echo "failed configurations:$failed"
  exit 1
fi
