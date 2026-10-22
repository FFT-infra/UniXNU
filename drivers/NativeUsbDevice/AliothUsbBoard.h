/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Read-only alioth USB board facts.
 *
 * Sources:
 *   alioth.dts:
 *     /soc/ssusb@a600000
 *     /soc/dwc3@a600000
 *     /soc/hsphy@88e3000
 *     /soc/ssusb@a800000 (disabled)
 *     /soc/ssphy@88e8000 (disabled)
 *   docs/alioth-driver-port-backlog.md
 *
 * This file contains only addresses, ranges, policy enums, and pure
 * validation logic. It performs no MMIO, clock, PHY, PDC, GDSC, cache,
 * interrupt, or kernel-service operation.
 */
#ifndef NATIVE_USB_DEVICE_ALIOTH_USB_BOARD_H
#define NATIVE_USB_DEVICE_ALIOTH_USB_BOARD_H

#include <stdint.h>

namespace native_usb {
namespace alioth {

constexpr uint64_t kMaxU64 = ~static_cast<uint64_t>(0);

struct AddressRange {
	uint64_t base;
	uint64_t size;
};

constexpr bool isValidRange(const AddressRange &range)
{
	return range.size != 0 && range.base <= kMaxU64 - range.size;
}

constexpr bool isValidRange(uint64_t base, uint64_t size)
{
	return isValidRange(AddressRange{base, size});
}

constexpr bool rangesEqual(const AddressRange &left, const AddressRange &right)
{
	return left.base == right.base && left.size == right.size;
}

constexpr bool rangeContains(const AddressRange &outer,
    const AddressRange &inner)
{
	if (!isValidRange(outer) || !isValidRange(inner) ||
	    inner.base < outer.base) {
		return false;
	}
	const uint64_t offset = inner.base - outer.base;
	return offset < outer.size && inner.size <= outer.size - offset;
}

constexpr bool rangeContainsAddress(const AddressRange &range,
    uint64_t address, uint64_t width)
{
	if (!isValidRange(range) || width == 0 ||
	    address > kMaxU64 - width || address < range.base) {
		return false;
	}
	const uint64_t offset = address - range.base;
	return offset < range.size && width <= range.size - offset;
}

constexpr bool rangeContainsAddress(const AddressRange &range,
    uint64_t address)
{
	return rangeContainsAddress(range, address, 1);
}

constexpr bool rangesDisjoint(const AddressRange &left,
    const AddressRange &right)
{
	if (!isValidRange(left) || !isValidRange(right)) {
		return false;
	}
	return left.base + left.size <= right.base ||
	    right.base + right.size <= left.base;
}

enum class UsbSpeed : uint8_t {
	kUnknown = 0,
	kFullSpeed = 1,
	kHighSpeed = 2,
	kSuperSpeed = 3,
};

struct ControllerFacts {
	AddressRange wrapper;
	AddressRange core;
	AddressRange hsPhy;
	AddressRange smmu;
	uint32_t smmuStreamId;
	uint32_t smmuStreamMask;
	UsbSpeed maximumSpeed;
	bool enabled;
	bool qmpPhyEnabled;
};

/*
 * The primary controller is the only one enabled for the first alioth USB
 * milestone. The DWC3 and HS PHY windows are deliberately separate from the
 * disabled SS/QMP path.
 */
inline constexpr AddressRange kWrapperRange = { 0x0a600000ULL, 0x00100000ULL };
inline constexpr AddressRange kPrimaryCoreRange = {
	0x0a600000ULL, 0x0000d93cULL
};
inline constexpr AddressRange kHsPhyRange = { 0x088e3000ULL, 0x00000110ULL };
inline constexpr AddressRange kAppsSmmuRange = {
	0x15000000ULL, 0x00100000ULL
};

inline constexpr uint64_t kWrapperBase = kWrapperRange.base;
inline constexpr uint64_t kWrapperSize = kWrapperRange.size;
inline constexpr uint64_t kDwc3CoreBase = kPrimaryCoreRange.base;
inline constexpr uint64_t kDwc3CoreSize = kPrimaryCoreRange.size;
inline constexpr uint64_t kHsPhyBase = kHsPhyRange.base;
inline constexpr uint64_t kHsPhySize = kHsPhyRange.size;
inline constexpr uint64_t kAppsSmmuBase = kAppsSmmuRange.base;
inline constexpr uint64_t kAppsSmmuSize = kAppsSmmuRange.size;

inline constexpr uint64_t kEudEnableBase = 0x088e2000ULL;
inline constexpr uint64_t kEudEnableSize = 0x00000004ULL;

inline constexpr uint32_t kPrimarySmmuStreamId = 0x00U;
inline constexpr uint32_t kPrimarySmmuStreamMask = 0x00U;
inline constexpr uint32_t kSecondarySmmuStreamId = 0x20U;
inline constexpr uint32_t kSecondarySmmuStreamMask = 0x00U;

inline constexpr ControllerFacts kPrimaryController = {
	kWrapperRange,
	kPrimaryCoreRange,
	kHsPhyRange,
	kAppsSmmuRange,
	kPrimarySmmuStreamId,
	kPrimarySmmuStreamMask,
	UsbSpeed::kHighSpeed,
	true,
	false,
};

inline constexpr AddressRange kSecondaryWrapperRange = {
	0x0a800000ULL, 0x00100000ULL
};
inline constexpr AddressRange kSecondaryCoreRange = {
	0x0a800000ULL, 0x0000d93cULL
};
inline constexpr AddressRange kSecondaryHsPhyRange = {
	0x088e4000ULL, 0x00000110ULL
};
inline constexpr AddressRange kQmpPhyRange = {
	0x088e8000ULL, 0x00003000ULL
};

inline constexpr ControllerFacts kSecondaryController = {
	kSecondaryWrapperRange,
	kSecondaryCoreRange,
	kSecondaryHsPhyRange,
	kAppsSmmuRange,
	kSecondarySmmuStreamId,
	kSecondarySmmuStreamMask,
	/* DTS says super-speed, but this controller is disabled on alioth. */
	UsbSpeed::kSuperSpeed,
	false,
	false,
};

inline constexpr bool kSecondaryControllerEnabled =
    kSecondaryController.enabled;
inline constexpr bool kQmpPhyEnabled = kPrimaryController.qmpPhyEnabled;
inline constexpr UsbSpeed kMaximumSpeed = kPrimaryController.maximumSpeed;

struct BoardFacts {
	ControllerFacts primary;
	ControllerFacts secondary;
	bool qmpPhyEnabled;
};

inline constexpr BoardFacts kAliothBoard = {
	kPrimaryController,
	kSecondaryController,
	false,
};

enum class PreflightIssue : uint32_t {
	kNone = 0,
	kWrapperRangeInvalid = 1U << 0,
	kWrapperRangeMismatch = 1U << 1,
	kCoreRangeInvalid = 1U << 2,
	kCoreRangeMismatch = 1U << 3,
	kCoreOutsideWrapper = 1U << 4,
	kHsPhyRangeInvalid = 1U << 5,
	kHsPhyRangeMismatch = 1U << 6,
	kAppsSmmuRangeInvalid = 1U << 7,
	kAppsSmmuRangeMismatch = 1U << 8,
	kSmmuStreamMismatch = 1U << 9,
	kSmmuStreamMaskMismatch = 1U << 10,
	kMaximumSpeedMismatch = 1U << 11,
	kPrimaryControllerDisabled = 1U << 12,
	kSecondaryControllerEnabled = 1U << 13,
	kQmpPhyEnabled = 1U << 14,
	kSecondarySmmuStreamMismatch = 1U << 15,
	kSecondarySmmuStreamMaskMismatch = 1U << 16,
};

constexpr PreflightIssue operator|(PreflightIssue left, PreflightIssue right)
{
	return static_cast<PreflightIssue>(
	    static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

struct PreflightResult {
	PreflightIssue issues;

	constexpr bool safe() const
	{
		return issues == PreflightIssue::kNone;
	}

	constexpr explicit operator bool() const
	{
		return safe();
	}

	constexpr bool has(PreflightIssue issue) const
	{
		return (static_cast<uint32_t>(issues) &
		    static_cast<uint32_t>(issue)) != 0;
	}
};

/*
 * Validate a readback snapshot without touching hardware. A caller must fill
 * BoardFacts from the live probe; the default only checks the static alioth
 * policy and is useful as a host-side baseline.
 */
constexpr PreflightResult preflightAlioth(const BoardFacts &observed =
    kAliothBoard)
{
	PreflightIssue issues = PreflightIssue::kNone;
	const ControllerFacts &primary = observed.primary;
	const ControllerFacts &secondary = observed.secondary;

	if (!isValidRange(primary.wrapper)) {
		issues = issues | PreflightIssue::kWrapperRangeInvalid;
	}
	if (!rangesEqual(primary.wrapper, kWrapperRange)) {
		issues = issues | PreflightIssue::kWrapperRangeMismatch;
	}
	if (!isValidRange(primary.core)) {
		issues = issues | PreflightIssue::kCoreRangeInvalid;
	}
	if (!rangesEqual(primary.core, kPrimaryCoreRange)) {
		issues = issues | PreflightIssue::kCoreRangeMismatch;
	}
	if (!rangeContains(primary.wrapper, primary.core)) {
		issues = issues | PreflightIssue::kCoreOutsideWrapper;
	}
	if (!isValidRange(primary.hsPhy)) {
		issues = issues | PreflightIssue::kHsPhyRangeInvalid;
	}
	if (!rangesEqual(primary.hsPhy, kHsPhyRange)) {
		issues = issues | PreflightIssue::kHsPhyRangeMismatch;
	}
	if (!isValidRange(primary.smmu)) {
		issues = issues | PreflightIssue::kAppsSmmuRangeInvalid;
	}
	if (!rangesEqual(primary.smmu, kAppsSmmuRange)) {
		issues = issues | PreflightIssue::kAppsSmmuRangeMismatch;
	}
	if (primary.smmuStreamId != kPrimarySmmuStreamId) {
		issues = issues | PreflightIssue::kSmmuStreamMismatch;
	}
	if (primary.smmuStreamMask != kPrimarySmmuStreamMask) {
		issues = issues | PreflightIssue::kSmmuStreamMaskMismatch;
	}
	if (primary.maximumSpeed != UsbSpeed::kHighSpeed) {
		issues = issues | PreflightIssue::kMaximumSpeedMismatch;
	}
	if (!primary.enabled) {
		issues = issues | PreflightIssue::kPrimaryControllerDisabled;
	}
	if (secondary.enabled != kSecondaryControllerEnabled) {
		issues = issues | PreflightIssue::kSecondaryControllerEnabled;
	}
	if (secondary.smmuStreamId != kSecondarySmmuStreamId) {
		issues = issues | PreflightIssue::kSecondarySmmuStreamMismatch;
	}
	if (secondary.smmuStreamMask != kSecondarySmmuStreamMask) {
		issues = issues |
		    PreflightIssue::kSecondarySmmuStreamMaskMismatch;
	}
	if (observed.qmpPhyEnabled != kQmpPhyEnabled) {
		issues = issues | PreflightIssue::kQmpPhyEnabled;
	}
	return { issues };
}

} // namespace alioth
} // namespace native_usb

#endif /* NATIVE_USB_DEVICE_ALIOTH_USB_BOARD_H */
