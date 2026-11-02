#!/bin/sh
# Cargo test runner on macOS: sign the test executable with the MTE entitlements and run it
codesign -s - -f --entitlements "$(dirname "$0")/mte-entitlements.plist" "$1" 2>/dev/null && exec "$@"
