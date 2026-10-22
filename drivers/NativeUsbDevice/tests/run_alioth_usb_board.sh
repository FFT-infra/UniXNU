#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu

cd "$(dirname "$0")"
output="${TMPDIR:-/tmp}/alioth-usb-board.$$"

if [ -z "${CXX:-}" ] && command -v xcrun >/dev/null 2>&1; then
	xcrun --sdk macosx clang++ -std=c++17 -Wall -Wextra -Werror \
		-pedantic -I .. test_alioth_usb_board.cpp -o "$output"
else
	"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
		-I .. test_alioth_usb_board.cpp -o "$output"
fi

"$output"
