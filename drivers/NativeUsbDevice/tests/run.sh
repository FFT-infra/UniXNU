#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause-Patent
set -eu

cd "$(dirname "$0")"
output="${TMPDIR:-/tmp}/alioth-native-usb-mechanics.$$"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
	-I .. test_usb_mechanics.cpp -o "$output"
"$output"
