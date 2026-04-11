#!/bin/sh -e 

#-----------------------------------------------------------------------------
# Bundle release
#-----------------------------------------------------------------------------

QUIET=""
FORCE=""
OSARCH=""
OSNAME=""
MI_TAG=""
MI_COMMIT=""

#---------------------------------------------------------
# Helper functions
#---------------------------------------------------------

info() {
  if [ -z "$QUIET" ] ; then
    echo "$@"
  fi
}

warn() {
  echo "$@" >&2
}

stop() {
  warn $@
  exit 1
}

has_cmd() {
  command -v "$1" > /dev/null 2>&1
}

on_path() {
  echo ":$PATH:" | grep -q :"$1":
}


#---------------------------------------------------------
# Detect OS and cpu architecture 
#---------------------------------------------------------

contains() {
  if echo "$1" | grep -i -E "$2" > /dev/null; then
    return 0
  else
    return 1
  fi
}

detect_osarch() {
  arch="$(uname -m)"
  case "$arch" in
    x86_64*|amd64*)
      arch="x64";;
    x86*|i[35678]86*)
      arch="x86";;
    arm64*|aarch64*|armv8*)
      arch="arm64";;
    arm*)
      arch="arm";;
    parisc*)
      arch="hppa";;
  esac

  OSNAME="linux"
  case "$(uname)" in
    [Ll]inux)
      OSNAME="linux";;
    [Dd]arwin)
      OSNAME="macos";;
    [Ff]ree[Bb][Ss][Dd])
      OSNAME="unix-freebsd";;
    [Oo]pen[Bb][Ss][Dd])
      OSNAME="unix-openbsd";;
    *)
      info "Warning: assuming generic Linux"
  esac
  OSARCH="$OSNAME-$arch"

  if [ "$OSNAME" = "linux" ]; then
    distrocfg=`cat $(find /etc/*-release -type f)`
    if contains "$distrocfg" "rhel"; then
      OSDISTRO="rhel"
    elif contains "$distrocfg" "opensuse"; then
      OSDISTRO="opensuse"
    elif contains "$distrocfg" "alpine"; then
      OSDISTRO="alpine"
    elif contains "$distrocfg" "arch"; then
      OSDISTRO="arch"
    elif contains "$distrocfg" "ubuntu|debian"; then
      OSDISTRO="ubuntu"
    else
      OSDISTRO="ubuntu" # default
    fi
  fi
}



#---------------------------------------------------------
# Command line options
#---------------------------------------------------------

process_options() {
  while : ; do
    flag="$1"
    case "$flag" in
    *=*)  flag_arg="${flag#*=}";;
    *)    flag_arg="yes" ;;
    esac
    # echo "option: $flag, arg: $flag_arg"
    case "$flag" in
      "") break;;
      -q|--quiet)
          QUIET="yes";;
      -p) shift
          PREFIX="$1";;
      -p=*|--prefix=*)
          PREFIX=`eval echo $flag_arg`;;  # no quotes so ~ gets expanded (issue #412)
      -t) shift
          MI_TAG="$1";;
      -t=*|--tag=*)
          MI_TAG="$flag_arg";;
      -h|--help|-\?|help|\?)
          MODE="help";;
      *) case "$flag" in
           *) warn "warning: unknown option \"$1\".";;
         esac;;
    esac
    shift
  done

  if [ -z "$MI_COMMIT" ] ; then
    MI_COMMIT=`git log -n 1 --pretty=format:"%H"`
  fi
  if [ -z "$MI_TAG" ] ; then
    MI_TAG=`git describe --tag`    
  fi
  info "Tag        : $MI_TAG"
  info "Commit     : $MI_COMMIT"
}



#---------------------------------------------------------
# Download 
#---------------------------------------------------------

download_failed() { # <program> <url>
  warn ""
  warn "unable to download: $2"  
  stop ""
}

download_file() {  # <url|file> <destination file>
  case "$1" in
    ftp://*|http://*|https://*)
      info "Downloading: $1"
      if has_cmd curl ; then
        if ! curl ${QUIET:+-sS} --proto =https --tlsv1.2 -f -L -o "$2" "$1"; then
          download_failed "curl" $1
        fi
      elif has_cmd wget ; then
        if ! wget ${QUIET:+-q} --https-only "-O$2" "$1"; then
          download_failed "wget" $1
        fi
      else
        stop "Neither 'curl' nor 'wget' is available; install one to continue."
      fi;;
    *)
      # echo "cp $1 to $2"
      info "Copying: $1"
      if ! cp $1 $2 ; then
        stop "Unable to copy from $1"
      fi;;
  esac
}

download_available() {  # <url|file>
  case "$1" in
    ftp://*|http://*|https://*)
      if has_cmd curl ; then
        if ! curl -sS --proto =https --tlsv1.2 -L -I "$1" | grep -E "^HTTP/2 200" ; then  # -I is headers only
          return 1
        fi
      fi;;
    *)
      if ! [ -f "$1" ] ; then
        return 1
      fi;;
  esac
  return 0
}

download_source_at_tag() { # <tag> <output file>
  download_file "https://github.com/microsoft/mimalloc/archive/refs/tags/$1.tar.gz" "$2"
}

download_source_at_commit() { # <commit> <output file>
  download_file "https://github.com/microsoft/mimalloc/archive/$1.tar.gz" "$2"
}

build_test_install() { # <type> <bundledir> <prefix> <cmake args>
  info "Build, test, install: $1 to $3"
  build_dir="$2/$1"
  mkdir -p "$build_dir"
  cmake . -B "$build_dir" $4
  cmake --build "$build_dir"
  ctest --test-dir "$build_dir" 
  cmake --install "$build_dir" --prefix "$3"
}
 
#---------------------------------------------------------
# Main
#---------------------------------------------------------

main_bundle() {
  # config
  bundle_dir="out/bundle"
  mkdir -p "$bundle_dir"

  # source archive
  info "Download source archive for $MI_TAG"
  source_archive="$bundle_dir/mimalloc-$MI_TAG-source.tar.gz"
  download_source_at_commit "$MI_COMMIT" "$source_archive"
  
  # build
  prefix_dir="$bundle_dir/prefix"
  build_test_install "debug"   "$bundle_dir" "$prefix_dir" "-DCMAKE_BUILD_TYPE=Debug"
  build_test_install "release" "$bundle_dir" "$prefix_dir" "-DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON"
  build_test_install "secure"  "$bundle_dir" "$prefix_dir" "-DCMAKE_BUILD_TYPE=Release -DMI_OPT_ARCH=ON -DMI_SECURE=ON"

  # archive binaries
  binary_archive_name="mimalloc-$MI_TAG-$OSARCH.tar.gz"
  binary_archive="$bundle_dir/$binary_archive_name"
  info "Create binary archive: $binary_archive_name"  
  pushd $bundle_dir
  tar -czvf "$binary_archive_name" prefix
  popd

  # done
  info ""
  info "Created:"  
  info "  - $binary_archive"
  info "  - $source_archive"
  info ""
  info "Done."  
}


main_help() {
  info "command:"
  info "  ./bin/bundle.sh [options]"
  info ""
  info "options:"
  info "  -q, --quiet              suppress output"
  info "  -f, --force              continue without prompting"
  info "  -p, --prefix=<dir>       prefix directory ($PREFIX)"
  info ""
}

main_start() {
  detect_osarch
  process_options $@
  if [ "$MODE" = "help" ] ; then
    main_help
  else
    main_bundle
  fi
}

# note: only start executing commands now to guard against partial downloads
main_start $@
