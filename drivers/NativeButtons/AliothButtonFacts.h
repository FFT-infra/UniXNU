/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef NATIVE_BUTTONS_ALIOTH_BUTTON_FACTS_H
#define NATIVE_BUTTONS_ALIOTH_BUTTON_FACTS_H

#include <stdint.h>

namespace native_buttons {

enum class Button : uint8_t {
	Power = 0,
	VolumeUp = 1,
	VolumeDown = 2,
	Count = 3
};

constexpr uint8_t buttonBit(Button button)
{
	return static_cast<uint8_t>(1u << static_cast<uint8_t>(button));
}

constexpr uint8_t kAllButtonBits =
    buttonBit(Button::Power) |
    buttonBit(Button::VolumeUp) |
    buttonBit(Button::VolumeDown);

struct RegisterWindow {
	const char *name;
	uint64_t address;
	uint64_t size;
};

/*
 * Facts extracted from uefi-alioth/Resources/DTBs/alioth.dts:
 * - 4172-4179: SPMI arbiter windows and EE 0.
 * - 4187-4198: PM8150 SID 0, PON base 0x800 and PPID 0x08.
 * - 4196-4218: KPDPWR and RESIN names/codes.
 * - 4259-4263: PM8150 GPIO controller and GPIO6 PPID 0xc5.
 * - 4319-4328 and 39116-39130: volume-up GPIO6, active-low, 15 ms.
 * - 3691-3699: PDC controller base. Its pin and IRQ are deliberately not
 *   represented here and must not be borrowed from another board.
 */
constexpr uint32_t kAliothSpmiSid = 0;
constexpr uint32_t kAliothSpmiExecutionEnvironment = 0;
constexpr uint32_t kAliothPonBase = 0x800;
constexpr uint32_t kAliothPonSize = 0x100;
constexpr uint32_t kAliothPonPpid = 0x08;
constexpr uint32_t kAliothPonKpdpwrLinuxCode = 0x74;
constexpr uint32_t kAliothPonResinLinuxCode = 0x72;
constexpr uint32_t kAliothPonDebounceUs = 0x3d09;

constexpr uint32_t kAliothPmicGpioBase = 0xc000;
constexpr uint32_t kAliothPmicGpioSize = 0xa00;
constexpr uint32_t kAliothVolumeUpGpio = 6;
constexpr uint32_t kAliothVolumeUpGpioPpid = 0xc5;
constexpr bool kAliothVolumeUpActiveLow = true;
constexpr uint32_t kAliothVolumeUpDebounceUs = 15u * 1000u;
constexpr uint32_t kAliothVolumeUpLinuxCode = 0x73;

constexpr uint64_t kAliothPdcControllerBase = 0xb220000;
constexpr uint64_t kAliothPdcControllerSize = 0x30000;

inline constexpr RegisterWindow kAliothSpmiWindows[] = {
	{"core", 0xc440000, 0x1100},
	{"chnls", 0xc600000, 0x2000000},
	{"obsrvr", 0xe600000, 0x100000},
	{"intr", 0xe700000, 0xa0000},
	{"cnfg", 0xc40a000, 0x26000},
};

constexpr Button kAliothKpdpwrButton = Button::Power;
constexpr Button kAliothResinButton = Button::VolumeDown;
constexpr Button kAliothGpio6Button = Button::VolumeUp;

static_assert(kAliothSpmiSid == 0);
static_assert(kAliothPonBase == 0x800);
static_assert(kAliothPonPpid == 0x08);
static_assert(kAliothVolumeUpGpio == 6);
static_assert(kAliothVolumeUpGpioPpid == 0xc5);
static_assert(kAliothVolumeUpActiveLow);
static_assert(kAllButtonBits == 0x07);

}  // namespace native_buttons

#endif
