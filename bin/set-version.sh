#!/bin/sh
# ----------------------------------------------------------------------------
# Copyright (c) 2018-2026, Microsoft Research, Daan Leijen
# This is free software; you can redistribute it and/or modify it under the
# terms of the MIT license. A copy of the license can be found in the file
# "LICENSE" at the root of this distribution.
# -----------------------------------------------------------------------------
# Set the mimalloc version in all files that contain it, or check that they agree.
#
#   bin/set-version.sh            # show the version in each file (and fail if they differ)
#   bin/set-version.sh 3.6.1      # set the version to 3.6.1
set -eu
cd "$(dirname "$0")/.."

header=include/mimalloc.h
cmake=cmake/mimalloc-config-version.cmake
vcpkg=contrib/vcpkg/vcpkg.json
cargo=contrib/rust/Cargo.toml
cargo_sys=contrib/rust/sys/Cargo.toml
rust_sys=contrib/rust/sys/src/lib.rs

# edit a file in place with `sed -E` (portable, as `sed -i` differs between GNU and BSD)
edit() {
  file=$1; shift
  sed -E "$@" "$file" > "$file.tmp"
  cat "$file.tmp" > "$file"
  rm "$file.tmp"
}

# print the first capture group of a `sed -E` pattern that matches a line in a file
get() {
  sed -n -E "s/$2/\1/p" "$1" | head -n 1
}

semver() {
  echo "$((${1:-0} / 10000)).$((${1:-0} / 100 % 100)).$((${1:-0} % 100))"
}

usage() {
  echo "usage: $0 [<major>.<minor>.<patch>]" >&2
  exit 2
}
if [ $# -gt 1 ]; then usage; fi
case "${1:-}" in -*) usage ;; esac

if [ $# -eq 1 ]; then
  if ! echo "$1" | grep -Eq '^[0-9]+\.([0-9]|[1-9][0-9])\.([0-9]|[1-9][0-9])$'; then
    echo "error: expecting a version <major>.<minor>.<patch> with minor and patch below 100" >&2
    exit 2
  fi
  ver=$1
  major=${ver%%.*}
  patch=${ver##*.}
  minor=${ver#*.}; minor=${minor%.*}
  num=$(printf "%d%02d%02d" "$major" "$minor" "$patch")

  edit "$header"    -e "s/^(#define MI_MALLOC_VERSION[[:space:]]+)[0-9]+/\1$num/"
  edit "$cmake"     -e "s/^set\(mi_version_major [0-9]+\)/set(mi_version_major $major)/" \
                    -e "s/^set\(mi_version_minor [0-9]+\)/set(mi_version_minor $minor)/" \
                    -e "s/^set\(mi_version_patch [0-9]+\)/set(mi_version_patch $patch)/"
  edit "$vcpkg"     -e "s/^(  \"version\": )\"[^\"]*\"/\1\"$ver\"/" \
                    -e "s/^(  \"port-version\": )[0-9]+/\10/"
  edit "$cargo"     -e "s/^version = \"[^\"]*\"/version = \"$ver\"/" \
                    -e "s/^(mimalloc-sys = \{ version = )\"=[^\"]*\"/\1\"=$ver\"/"
  edit "$cargo_sys" -e "s/^version = \"[^\"]*\"/version = \"$ver\"/"
  edit "$rust_sys"  -e "s/^(pub const MI_MALLOC_VERSION: c_int = )[0-9]+;/\1$num;/"
fi

# show and check the version in each file
versions=""
show() {
  printf "%-40s %s\n" "$1" "$2"
  versions="$versions$2
"
}
show "$header" "$(semver "$(get "$header" '^#define MI_MALLOC_VERSION[[:space:]]+([0-9]+).*')")"
show "$cmake" "$(get "$cmake" '^set\(mi_version_major ([0-9]+)\)').$(get "$cmake" '^set\(mi_version_minor ([0-9]+)\)').$(get "$cmake" '^set\(mi_version_patch ([0-9]+)\)')"
show "$vcpkg" "$(get "$vcpkg" '^  "version": "([^"]*)".*')"
show "$cargo" "$(get "$cargo" '^version = "([^"]*)".*')"
show "$cargo (mimalloc-sys)" "$(get "$cargo" '^mimalloc-sys = \{ version = "=([^"]*)".*')"
show "$cargo_sys" "$(get "$cargo_sys" '^version = "([^"]*)".*')"
show "$rust_sys" "$(semver "$(get "$rust_sys" '^pub const MI_MALLOC_VERSION: c_int = ([0-9]+);.*')")"

if [ "$(printf "%s" "$versions" | sort -u | wc -l)" -ne 1 ]; then
  echo "error: the versions differ" >&2
  exit 1
fi
if [ $# -eq 1 ]; then
  echo
  echo "also update by hand: the release notes and \"Latest release\" in readme.md,"
  echo "and the SHA512 in contrib/vcpkg/portfile.cmake (after tagging v$1)."
fi
