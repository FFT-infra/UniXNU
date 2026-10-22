/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only checks for discovery data and button event logic. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ButtonEventLogic.h"
#include "SpmIDiscovery.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
		exit(1); \
	} \
} while (0)

namespace {

uint32_t makeMap(uint32_t ppid, bool irqOwner)
{
	return 1u | (ppid << 8) | (irqOwner ? (1u << 24) : 0u);
}

native_buttons::U32Observation runtimeObservation(uint32_t value)
{
	return {true, native_buttons::Evidence::RuntimeReadback, value};
}

void testAliothFacts()
{
	using namespace native_buttons;

	CHECK(kAliothSpmiSid == 0);
	CHECK(kAliothSpmiExecutionEnvironment == 0);
	CHECK(kAliothPonBase == 0x800);
	CHECK(kAliothPonSize == 0x100);
	CHECK(kAliothPonPpid == 0x08);
	CHECK(kAliothPonDebounceUs == 15625);
	CHECK(kAliothPmicGpioBase == 0xc000);
	CHECK(kAliothPmicGpioSize == 0xa00);
	CHECK(kAliothVolumeUpGpio == 6);
	CHECK(kAliothVolumeUpGpioPpid == 0xc5);
	CHECK(kAliothVolumeUpActiveLow);
	CHECK(kAliothVolumeUpDebounceUs == 15000);
	CHECK(kAliothPdcControllerBase == 0xb220000);
	CHECK(kAliothPdcControllerSize == 0x30000);
	CHECK(kAliothSpmiWindows[0].address == 0xc440000);
	CHECK(kAliothSpmiWindows[4].address == 0xc40a000);
	CHECK(kAliothPonKpdpwrLinuxCode == 0x74);
	CHECK(kAliothPonResinLinuxCode == 0x72);
	CHECK(kAliothVolumeUpLinuxCode == 0x73);
	CHECK(kAliothKpdpwrButton == Button::Power);
	CHECK(kAliothResinButton == Button::VolumeDown);
	CHECK(kAliothGpio6Button == Button::VolumeUp);
	printf("alioth DTS facts: PASS\n");
}

void testApidDecodeAndLookup()
{
	using namespace native_buttons;

	DecodedApidMap decoded = decodeApidMap(makeMap(0x08, true));
	CHECK(decoded.present);
	CHECK(decoded.ppid == 0x08);
	CHECK(decoded.irqOwner);
	CHECK(!decodeApidMap(0).present);
	CHECK(!decodeApidMap(makeMap(0x08, false)).irqOwner);

	ApidMapEntry map[4] = {
		{0, makeMap(0x10, true), 0, true, Evidence::RuntimeReadback},
		{1, makeMap(kAliothPonPpid, true), 0, true,
		    Evidence::RuntimeReadback},
		{2, makeMap(0x20, true), 1, true, Evidence::RuntimeReadback},
		{3, makeMap(kAliothVolumeUpGpioPpid, true), 0, true,
		    Evidence::RuntimeReadback},
	};

	ApidLookup lookup = findUniqueApid(map, 4, true, kAliothPonPpid, 0);
	CHECK(lookup.state == ApidLookupState::Found);
	CHECK(lookup.apid == 1);
	CHECK(lookup.matchCount == 1);

	lookup = findUniqueApid(map, 4, false, kAliothPonPpid, 0);
	CHECK(lookup.state == ApidLookupState::NotScanned);
	CHECK(lookup.apid == kInvalidApid);

	lookup = findUniqueApid(map, 4, true, 0x33, 0);
	CHECK(lookup.state == ApidLookupState::NotFound);
	CHECK(lookup.apid == kInvalidApid);

	map[1].rawMap = makeMap(kAliothPonPpid, false);
	lookup = findUniqueApid(map, 4, true, kAliothPonPpid, 0);
	CHECK(lookup.state == ApidLookupState::NotFound);

	map[1].rawMap = makeMap(kAliothPonPpid, true);
	map[2].rawMap = makeMap(kAliothPonPpid, true);
	map[2].ownerEe = 0;
	lookup = findUniqueApid(map, 4, true, kAliothPonPpid, 0);
	CHECK(lookup.state == ApidLookupState::Ambiguous);
	CHECK(lookup.apid == kInvalidApid);
	CHECK(lookup.matchCount == 2);

	map[2].ownerEe = 1;
	map[3].evidence = Evidence::AliothDeviceTree;
	lookup = findUniqueApid(map, 4, true, kAliothPonPpid, 0);
	CHECK(lookup.state == ApidLookupState::NotScanned);
	map[3].evidence = Evidence::RuntimeReadback;
	map[3].ownerEe = 8;
	lookup = findUniqueApid(
	    map, 4, true, kAliothVolumeUpGpioPpid, 0);
	CHECK(lookup.state == ApidLookupState::NotScanned);
	printf("APID decode and unique lookup: PASS\n");
}

native_buttons::ButtonDiscoveryInput completeInput()
{
	using namespace native_buttons;

	static const ApidMapEntry map[6] = {
		{0, makeMap(0x10, true), 0, true, Evidence::RuntimeReadback},
		{1, makeMap(kAliothPonPpid, true), 0, true,
		    Evidence::RuntimeReadback},
		{2, makeMap(0x20, true), 0, true, Evidence::RuntimeReadback},
		{3, makeMap(kAliothVolumeUpGpioPpid, true), 0, true,
		    Evidence::RuntimeReadback},
		{4, makeMap(0x40, true), 0, true, Evidence::RuntimeReadback},
		{5, makeMap(0x50, true), 0, true, Evidence::RuntimeReadback},
	};

	ButtonDiscoveryInput input = {};
	input.arbiterVersion = runtimeObservation(0x70000000u);
	input.features = runtimeObservation(6);
	input.pdcVersion = runtimeObservation(0x30200);
	input.apidScanComplete = true;
	input.apidMap = map;
	input.apidMapCount = 6;
	input.pdcRoute = {true, Evidence::RuntimeReadback, 42, 0x1357};
	return input;
}

void testDiscoveryReporting()
{
	using namespace native_buttons;

	ButtonDiscoveryInput input = {};
	ButtonDiscoveryReport report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockArbiterVersionMissing) != 0);
	CHECK((report.blockerMask & kBlockFeaturesMissing) != 0);
	CHECK((report.blockerMask & kBlockPdcRouteMissing) != 0);
	CHECK(report.pon.state == ApidLookupState::NotScanned);
	CHECK(report.gpio6.state == ApidLookupState::NotScanned);
	CHECK(report.pon.apid == kInvalidApid);
	CHECK(report.gpio6.apid == kInvalidApid);

	input = completeInput();
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Ready);
	CHECK(report.blockerMask == 0);
	CHECK(report.arbiterVersion == 0x70000000u);
	CHECK(report.apidCount == 6);
	CHECK(report.pon.state == ApidLookupState::Found);
	CHECK(report.pon.apid == 1);
	CHECK(report.gpio6.state == ApidLookupState::Found);
	CHECK(report.gpio6.apid == 3);
	CHECK(report.pdcRoute.pin == 42);
	CHECK(report.pdcRoute.irq == 0x1357);

	input = completeInput();
	input.arbiterVersion = runtimeObservation(0x60000000u);
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockArbiterVersionUnsupported) != 0);

	input = completeInput();
	input.features = runtimeObservation(7);
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockApidScanIncomplete) != 0);

	input = completeInput();
	input.apidMapCount = 5;
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockApidScanIncomplete) != 0);

	input = completeInput();
	input.pdcRoute = {};
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockPdcRouteMissing) != 0);

	input = completeInput();
	input.pdcRoute.evidence = Evidence::AliothDeviceTree;
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockPdcRouteMissing) != 0);

	input = completeInput();
	input.pdcVersion.evidence = Evidence::AliothDeviceTree;
	report = evaluateButtonDiscovery(input);
	CHECK(report.state == ProbeState::Blocked);
	CHECK((report.blockerMask & kBlockPdcVersionMissing) != 0);
	printf("discovery blockers and readiness: PASS\n");
}

void testReadOnlyPlan()
{
	using namespace native_buttons;

	ButtonDiscoveryInput emptyInput = {};
	ButtonDiscoveryReport blocked = evaluateButtonDiscovery(emptyInput);
	ReadOnlyPlan plan = makeReadOnlyPlan(blocked);
	CHECK(plan.actionCount == 3);
	CHECK(!plan.blockersClear());
	CHECK(plan.actions[0].purpose == ReadPurpose::ArbiterVersion);
	CHECK(plan.actions[0].offset == 0);
	CHECK(plan.actions[1].purpose == ReadPurpose::ArbiterFeatures);
	CHECK(plan.actions[1].offset == kSpmIFeaturesOffset);
	CHECK(plan.actions[2].purpose == ReadPurpose::PdcVersion);
	CHECK(plan.actions[2].offset == kPdcVersionOffset);

	ButtonDiscoveryReport ready =
	    evaluateButtonDiscovery(completeInput());
	CHECK(ready.state == ProbeState::Ready);
	plan = makeReadOnlyPlan(ready);
	CHECK(plan.actionCount == 7);
	CHECK(plan.blockersClear());
	CHECK(plan.actions[3].purpose == ReadPurpose::ApidMapWord);
	CHECK(plan.actions[3].offset == kSpmIApidMapOffset);
	CHECK(plan.actions[3].count == 6);
	CHECK(plan.actions[4].purpose == ReadPurpose::ApidOwnerWord);
	CHECK(plan.actions[4].offset == kSpmIConfigOwnerOffset);
	CHECK(plan.actions[5].purpose == ReadPurpose::PonRtStatus);
	CHECK(plan.actions[5].apid == 1);
	CHECK(plan.actions[5].width == ReadWidth::U8);
	CHECK(plan.actions[6].purpose == ReadPurpose::Gpio6RtStatus);
	CHECK(plan.actions[6].apid == 3);
	CHECK(plan.actions[6].width == ReadWidth::U8);
	printf("read-only plan and dynamic ranges: PASS\n");
}

void testDebounceAndHeldButtons()
{
	using namespace native_buttons;

	ButtonBank bad;
	CHECK(!bad.init({0, 1, 1}));
	CHECK(bad.update(buttonBit(Button::Power), 0).empty());

	ButtonBank buttons;
	CHECK(buttons.init(kAliothButtonDebounceConfig));

	const uint8_t power = buttonBit(Button::Power);
	ButtonTransitions transitions = buttons.update(power, 0);
	CHECK(transitions.empty());
	transitions = buttons.update(power, kAliothPonDebounceUs - 1);
	CHECK(transitions.empty());
	transitions = buttons.update(power, kAliothPonDebounceUs);
	CHECK(transitions.pressedMask == power);
	CHECK(transitions.releasedMask == 0);

	/* A held button remains pressed and never repeats. */
	for (uint64_t now = kAliothPonDebounceUs + 1;
	    now < 10000000;
	    now += 100000) {
		CHECK(buttons.update(power, now).empty());
	}

	transitions = buttons.update(0, 10000001);
	CHECK(transitions.empty());
	transitions = buttons.update(0, 10000001 + kAliothPonDebounceUs);
	CHECK(transitions.pressedMask == 0);
	CHECK(transitions.releasedMask == power);
	CHECK(buttons.update(0, 20000000).empty());

	/* Bounce restarts the candidate interval instead of extending it. */
	transitions = buttons.update(power, 30000000);
	CHECK(transitions.empty());
	transitions = buttons.update(0, 30005000);
	CHECK(transitions.empty());
	transitions = buttons.update(power, 30010000);
	CHECK(transitions.empty());
	transitions = buttons.update(
	    power, 30010000 + kAliothPonDebounceUs - 1);
	CHECK(transitions.empty());
	transitions = buttons.update(
	    power, 30010000 + kAliothPonDebounceUs);
	CHECK(transitions.pressedMask == power);
	printf("debounce, bounce restart, and held no-repeat: PASS\n");
}

void testIndependentButtonsAndTimeRejection()
{
	using namespace native_buttons;

	ButtonBank buttons;
	CHECK(buttons.init(kAliothButtonDebounceConfig));
	const uint8_t all =
	    buttonBit(Button::Power) |
	    buttonBit(Button::VolumeUp) |
	    buttonBit(Button::VolumeDown);

	ButtonTransitions transitions = buttons.update(all, 1000);
	CHECK(transitions.empty());
	transitions = buttons.update(
	    all, 1000 + kAliothVolumeUpDebounceUs);
	CHECK(transitions.pressedMask ==
	    (buttonBit(Button::VolumeUp) | buttonBit(Button::VolumeDown)));
	CHECK(transitions.releasedMask == 0);
	transitions = buttons.update(
	    all, 1000 + kAliothPonDebounceUs);
	CHECK(transitions.pressedMask == buttonBit(Button::Power));
	CHECK(transitions.releasedMask == 0);

	/* Time moving backwards is rejected without changing stable state. */
	transitions = buttons.update(0, 1000);
	CHECK(transitions.empty());
	transitions = buttons.update(
	    0, 1000 + kAliothPonDebounceUs + kAliothPonDebounceUs);
	CHECK(transitions.empty());
	transitions = buttons.update(
	    0, 1000 + kAliothPonDebounceUs +
	    kAliothPonDebounceUs * 2);
	CHECK(transitions.releasedMask ==
	    (buttonBit(Button::Power) |
	     buttonBit(Button::VolumeUp) |
	     buttonBit(Button::VolumeDown)));
	printf("independent buttons and backward-time rejection: PASS\n");
}

}  // namespace

int main()
{
	testAliothFacts();
	testApidDecodeAndLookup();
	testDiscoveryReporting();
	testReadOnlyPlan();
	testDebounceAndHeldButtons();
	testIndependentButtonsAndTimeRejection();
	printf("ALL PASS\n");
	return 0;
}
