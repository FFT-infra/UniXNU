/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef NATIVE_BUTTONS_SPMI_DISCOVERY_H
#define NATIVE_BUTTONS_SPMI_DISCOVERY_H

#include <stddef.h>
#include <stdint.h>

#include "AliothButtonFacts.h"

namespace native_buttons {

enum class Evidence : uint8_t {
	Unknown = 0,
	AliothDeviceTree,
	RuntimeReadback
};

struct U32Observation {
	bool present;
	Evidence evidence;
	uint32_t value;
};

struct ApidMapEntry {
	uint16_t apid;
	uint32_t rawMap;
	uint8_t ownerEe;
	bool ownerObserved;
	Evidence evidence;
};

struct DecodedApidMap {
	bool present;
	uint16_t ppid;
	bool irqOwner;
};

enum class ApidLookupState : uint8_t {
	NotScanned = 0,
	NotFound,
	Ambiguous,
	Found
};

constexpr uint16_t kInvalidApid = UINT16_MAX;

struct ApidLookup {
	ApidLookupState state;
	uint16_t apid;
	uint32_t matchCount;
};

struct PdcRouteObservation {
	bool discovered;
	Evidence evidence;
	uint32_t pin;
	uint32_t irq;
};

struct ButtonDiscoveryInput {
	U32Observation arbiterVersion;
	U32Observation features;
	U32Observation pdcVersion;
	bool apidScanComplete;
	const ApidMapEntry *apidMap;
	size_t apidMapCount;
	PdcRouteObservation pdcRoute;
};

enum : uint32_t {
	kBlockArbiterVersionMissing = 1u << 0,
	kBlockArbiterVersionUnsupported = 1u << 1,
	kBlockFeaturesMissing = 1u << 2,
	kBlockApidCountInvalid = 1u << 3,
	kBlockApidScanIncomplete = 1u << 4,
	kBlockApidScanInvalid = 1u << 5,
	kBlockPonApidMissing = 1u << 6,
	kBlockPonApidAmbiguous = 1u << 7,
	kBlockGpio6ApidMissing = 1u << 8,
	kBlockGpio6ApidAmbiguous = 1u << 9,
	kBlockPdcVersionMissing = 1u << 10,
	kBlockPdcRouteMissing = 1u << 11,
	kBlockReadPlanOverflow = 1u << 12,
};

enum class ProbeState : uint8_t {
	Blocked = 0,
	Ready
};

struct ButtonDiscoveryReport {
	ProbeState state;
	uint32_t blockerMask;
	uint32_t arbiterVersion;
	uint32_t apidCount;
	uint32_t pdcVersion;
	ApidLookup pon;
	ApidLookup gpio6;
	PdcRouteObservation pdcRoute;
};

/*
 * Qualcomm SPMI arbiter v7 and PON/GPIO register-layout facts used only for
 * read planning. These describe the decoder, not an alioth APID or PDC route.
 */
constexpr uint32_t kSpmIVersionV7Min = 0x70000000u;
constexpr uint32_t kSpmIVersionV8Min = 0x80000000u;
constexpr uint32_t kSpmIFeaturesOffset = 0x04;
constexpr uint32_t kSpmIFeaturesApidMask = 0x7ff;
constexpr uint32_t kSpmIApidMapOffset = 0x2000;
constexpr uint32_t kSpmIConfigOwnerOffset = 0x00;
constexpr uint32_t kSpmIObserverApidStride = 0x20;
constexpr uint32_t kSpmIObserverEeStride = 0x8000;
constexpr uint32_t kSpmIPonRtStatusOffset = 0x10;
constexpr uint32_t kSpmIGpioRtStatusOffset = 0x10;
constexpr uint32_t kPdcVersionOffset = 0x1000;
constexpr uint32_t kMaximumSpmIApids = 1024;

constexpr DecodedApidMap decodeApidMap(uint32_t rawMap)
{
	DecodedApidMap decoded = {};
	if (rawMap == 0) {
		return decoded;
	}
	decoded.present = true;
	decoded.ppid = static_cast<uint16_t>((rawMap >> 8) & 0xfffu);
	decoded.irqOwner = ((rawMap >> 24) & 1u) != 0;
	return decoded;
}

constexpr bool observedAtRuntime(U32Observation observation)
{
	return observation.present &&
	    observation.evidence == Evidence::RuntimeReadback;
}

inline ApidLookup findUniqueApid(
    const ApidMapEntry *map,
    size_t count,
    bool scanComplete,
    uint16_t ppid,
    uint8_t executionEnvironment)
{
	ApidLookup result = {ApidLookupState::NotScanned, kInvalidApid, 0};
	if (!scanComplete || map == nullptr || count == 0) {
		return result;
	}

	/*
	 * APID placement cannot come from a static DT. Refuse a partial or
	 * unobserved table rather than selecting a plausible entry.
	 */
	for (size_t i = 0; i < count; i++) {
		if (map[i].evidence != Evidence::RuntimeReadback ||
		    !map[i].ownerObserved ||
		    map[i].ownerEe > 7 ||
		    map[i].apid != static_cast<uint16_t>(i)) {
			result.state = ApidLookupState::NotScanned;
			result.apid = kInvalidApid;
			result.matchCount = 0;
			return result;
		}
	}

	for (size_t i = 0; i < count; i++) {
		const DecodedApidMap decoded = decodeApidMap(map[i].rawMap);
		if (!decoded.present ||
		    decoded.ppid != ppid ||
		    !decoded.irqOwner ||
		    map[i].ownerEe != executionEnvironment) {
			continue;
		}
		result.matchCount++;
		if (result.matchCount == 1) {
			result.apid = map[i].apid;
		}
	}

	if (result.matchCount == 0) {
		result.state = ApidLookupState::NotFound;
	} else if (result.matchCount == 1) {
		result.state = ApidLookupState::Found;
	} else {
		result.state = ApidLookupState::Ambiguous;
		result.apid = kInvalidApid;
	}
	return result;
}

inline uint32_t apidBlockerFor(
    ApidLookup lookup,
    uint32_t missingBlocker,
    uint32_t ambiguousBlocker)
{
	switch (lookup.state) {
	case ApidLookupState::Found:
		return 0;
	case ApidLookupState::Ambiguous:
		return ambiguousBlocker;
	case ApidLookupState::NotScanned:
	case ApidLookupState::NotFound:
	default:
		return missingBlocker;
	}
}

inline ButtonDiscoveryReport evaluateButtonDiscovery(
    const ButtonDiscoveryInput &input)
{
	ButtonDiscoveryReport report = {};
	report.state = ProbeState::Blocked;
	report.pdcRoute = input.pdcRoute;
	report.pon = {ApidLookupState::NotScanned, kInvalidApid, 0};
	report.gpio6 = {ApidLookupState::NotScanned, kInvalidApid, 0};

	uint32_t blockers = 0;
	bool versionValid = false;
	bool apidCountValid = false;

	if (!observedAtRuntime(input.arbiterVersion)) {
		blockers |= kBlockArbiterVersionMissing;
	} else {
		report.arbiterVersion = input.arbiterVersion.value;
		versionValid = report.arbiterVersion >= kSpmIVersionV7Min &&
		    report.arbiterVersion < kSpmIVersionV8Min;
		if (!versionValid) {
			blockers |= kBlockArbiterVersionUnsupported;
		}
	}

	if (!observedAtRuntime(input.features)) {
		blockers |= kBlockFeaturesMissing;
	} else {
		report.apidCount = input.features.value & kSpmIFeaturesApidMask;
		apidCountValid = report.apidCount != 0 &&
		    report.apidCount <= kMaximumSpmIApids;
		if (!apidCountValid) {
			blockers |= kBlockApidCountInvalid;
		}
	}

	bool scanValid = false;
	if (!input.apidScanComplete ||
	    input.apidMap == nullptr ||
	    !apidCountValid ||
	    input.apidMapCount != report.apidCount) {
		blockers |= kBlockApidScanIncomplete;
	} else {
		scanValid = true;
		for (size_t i = 0; i < input.apidMapCount; i++) {
			if (input.apidMap[i].evidence != Evidence::RuntimeReadback ||
			    !input.apidMap[i].ownerObserved ||
			    input.apidMap[i].ownerEe > 7 ||
			    input.apidMap[i].apid != static_cast<uint16_t>(i)) {
				scanValid = false;
				blockers |= kBlockApidScanInvalid;
				break;
			}
		}
	}

	if (versionValid && apidCountValid && scanValid) {
		report.pon = findUniqueApid(
		    input.apidMap,
		    input.apidMapCount,
		    true,
		    static_cast<uint16_t>(kAliothPonPpid),
		    static_cast<uint8_t>(kAliothSpmiExecutionEnvironment));
		report.gpio6 = findUniqueApid(
		    input.apidMap,
		    input.apidMapCount,
		    true,
		    static_cast<uint16_t>(kAliothVolumeUpGpioPpid),
		    static_cast<uint8_t>(kAliothSpmiExecutionEnvironment));
	}

	blockers |= apidBlockerFor(
	    report.pon, kBlockPonApidMissing, kBlockPonApidAmbiguous);
	blockers |= apidBlockerFor(
	    report.gpio6, kBlockGpio6ApidMissing, kBlockGpio6ApidAmbiguous);

	if (!observedAtRuntime(input.pdcVersion)) {
		blockers |= kBlockPdcVersionMissing;
	} else {
		report.pdcVersion = input.pdcVersion.value;
	}

	if (!input.pdcRoute.discovered ||
	    input.pdcRoute.evidence != Evidence::RuntimeReadback) {
		blockers |= kBlockPdcRouteMissing;
	}

	report.blockerMask = blockers;
	report.state = blockers == 0 ? ProbeState::Ready : ProbeState::Blocked;
	return report;
}

enum class RegisterSpace : uint8_t {
	SpmiCore = 0,
	SpmiConfig,
	SpmiObserver,
	Pdc
};

enum class ReadWidth : uint8_t {
	U8 = 0,
	U32
};

enum class ReadPurpose : uint8_t {
	ArbiterVersion = 0,
	ArbiterFeatures,
	ApidMapWord,
	ApidOwnerWord,
	PonRtStatus,
	Gpio6RtStatus,
	PdcVersion
};

struct ReadAction {
	RegisterSpace space;
	ReadWidth width;
	ReadPurpose purpose;
	uint32_t offset;
	uint32_t stride;
	uint32_t count;
	uint16_t apid;
};

constexpr size_t kMaximumReadActions = 8;

struct ReadOnlyPlan {
	uint32_t blockerMask;
	size_t actionCount;
	ReadAction actions[kMaximumReadActions];

	constexpr bool blockersClear() const
	{
		return blockerMask == 0 &&
		    actionCount != 0 &&
		    actionCount <= kMaximumReadActions;
	}
};

/*
 * This type intentionally has no write direction or write action. Adding a
 * MMIO store requires changing the data model, rather than setting a flag.
 */
inline ReadOnlyPlan makeReadOnlyPlan(const ButtonDiscoveryReport &report)
{
	ReadOnlyPlan plan = {};
	plan.blockerMask = report.blockerMask;
	const auto add = [&plan](
	    RegisterSpace space,
	    ReadWidth width,
	    ReadPurpose purpose,
	    uint32_t offset,
	    uint32_t stride,
	    uint32_t count,
	    uint16_t apid) {
		if (plan.actionCount >= kMaximumReadActions) {
			plan.blockerMask |= kBlockReadPlanOverflow;
			return;
		}
		plan.actions[plan.actionCount++] =
		    {space, width, purpose, offset, stride, count, apid};
	};

	add(RegisterSpace::SpmiCore, ReadWidth::U32,
	    ReadPurpose::ArbiterVersion, 0, 0, 1, kInvalidApid);
	add(RegisterSpace::SpmiCore, ReadWidth::U32,
	    ReadPurpose::ArbiterFeatures, kSpmIFeaturesOffset, 0, 1,
	    kInvalidApid);
	add(RegisterSpace::Pdc, ReadWidth::U32,
	    ReadPurpose::PdcVersion, kPdcVersionOffset, 0, 1, kInvalidApid);

	if (report.apidCount != 0 &&
	    report.apidCount <= kMaximumSpmIApids) {
		add(RegisterSpace::SpmiCore, ReadWidth::U32,
		    ReadPurpose::ApidMapWord, kSpmIApidMapOffset, 4,
		    report.apidCount, kInvalidApid);
		add(RegisterSpace::SpmiConfig, ReadWidth::U32,
		    ReadPurpose::ApidOwnerWord, kSpmIConfigOwnerOffset, 4,
		    report.apidCount, kInvalidApid);
	}

	if (report.pon.state == ApidLookupState::Found) {
		add(RegisterSpace::SpmiObserver, ReadWidth::U8,
		    ReadPurpose::PonRtStatus, kSpmIPonRtStatusOffset, 0, 1,
		    report.pon.apid);
	}
	if (report.gpio6.state == ApidLookupState::Found) {
		add(RegisterSpace::SpmiObserver, ReadWidth::U8,
		    ReadPurpose::Gpio6RtStatus, kSpmIGpioRtStatusOffset, 0, 1,
		    report.gpio6.apid);
	}
	return plan;
}

static_assert(kMaximumReadActions >= 7);
static_assert(kSpmIObserverApidStride == 0x20);
static_assert(kSpmIObserverEeStride == 0x8000);

}  // namespace native_buttons

#endif
