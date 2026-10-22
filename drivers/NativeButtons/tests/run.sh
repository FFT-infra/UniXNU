#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu

cd "$(dirname "$0")"

if [ -n "${CXX:-}" ]; then
	compiler=$CXX
	sdkroot=
elif command -v xcrun >/dev/null 2>&1 &&
    xcrun --find clang++ >/dev/null 2>&1; then
	compiler=$(xcrun --find clang++)
	sdkroot=$(xcrun --sdk macosx --show-sdk-path)
else
	compiler=c++
	sdkroot=
fi

output_dir=$(mktemp -d "${TMPDIR:-/tmp}/alioth-native-buttons.XXXXXX")
trap 'rm -rf "$output_dir"' EXIT HUP INT TERM

common_flags="-std=c++17 -Wall -Wextra -Werror -pedantic -I .."

compile()
{
	if [ -n "$sdkroot" ]; then
		"$compiler" $common_flags -isysroot "$sdkroot" "$@"
	else
		"$compiler" $common_flags "$@"
	fi
}

compile test_button_logic.cpp -o "$output_dir/test-buttons"
"$output_dir/test-buttons"

compile -fno-omit-frame-pointer \
	-fsanitize=address,undefined test_button_logic.cpp \
	-o "$output_dir/test-buttons-sanitized"
"$output_dir/test-buttons-sanitized"
