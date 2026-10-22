/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only checks for alioth board facts and the DWC3 layout layer. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "AliothDwc3Layout.h"
#include "AliothUsbBoard.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
		exit(1); \
	} \
} while (0)

namespace {

void testBoardFacts()
{
	using namespace native_usb::alioth;

	CHECK(kWrapperBase == 0x0a600000ULL);
	CHECK(kDwc3CoreBase == 0x0a600000ULL);
	CHECK(kDwc3CoreSize == 0x0000d93cULL);
	CHECK(kHsPhyBase == 0x088e3000ULL);
	CHECK(kHsPhySize == 0x00000110ULL);
	CHECK(kPrimarySmmuStreamId == 0x00U);
	CHECK(kPrimarySmmuStreamMask == 0x00U);
	CHECK(kSecondarySmmuStreamId == 0x20U);
	CHECK(kMaximumSpeed == UsbSpeed::kHighSpeed);
	CHECK(!kSecondaryControllerEnabled);
	CHECK(!kQmpPhyEnabled);
	CHECK(isValidRange(kWrapperRange));
	CHECK(isValidRange(kPrimaryCoreRange));
	CHECK(isValidRange(kHsPhyRange));
	CHECK(rangeContains(kWrapperRange, kPrimaryCoreRange));
	CHECK(rangeContainsAddress(kHsPhyRange, kHsPhyBase, kHsPhySize));
	CHECK(!rangeContainsAddress(kHsPhyRange,
	    kHsPhyBase + kHsPhySize, 1));
	printf("alioth board facts: PASS\n");
}

void testAddressRanges()
{
	using namespace native_usb::alioth;

	const AddressRange outer = { 0x1000, 0x1000 };
	const AddressRange inner = { 0x1200, 0x100 };
	const AddressRange outside = { 0x2200, 0x10 };
	const AddressRange overflow = { ~static_cast<uint64_t>(0), 2 };
	const AddressRange zero = { 0x1000, 0 };

	CHECK(isValidRange(outer));
	CHECK(!isValidRange(overflow));
	CHECK(!isValidRange(zero));
	CHECK(rangeContains(outer, inner));
	CHECK(!rangeContains(outer, outside));
	CHECK(!rangeContains(overflow, inner));
	CHECK(rangeContainsAddress(outer, 0x1000, 0x1000));
	CHECK(!rangeContainsAddress(outer, 0x2000, 1));
	CHECK(!rangeContainsAddress(outer, ~static_cast<uint64_t>(0), 2));
	CHECK(!rangeContainsAddress(outer, 0x1000, 0));
	CHECK(rangesDisjoint(outer, outside));
	CHECK(!rangesDisjoint(outer, inner));
	CHECK(!rangesDisjoint(overflow, inner));
	printf("address ranges: PASS\n");
}

void testPreflight()
{
	using namespace native_usb::alioth;

	BoardFacts facts = kAliothBoard;
	PreflightResult result = preflightAlioth(facts);
	CHECK(result.safe());
	CHECK(static_cast<bool>(result));
	CHECK(!result.has(PreflightIssue::kSmmuStreamMismatch));

	facts.primary.smmuStreamId = 0x60;
	result = preflightAlioth(facts);
	CHECK(!result.safe());
	CHECK(result.has(PreflightIssue::kSmmuStreamMismatch));

	facts = kAliothBoard;
	facts.primary.smmuStreamMask = 0x1;
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kSmmuStreamMaskMismatch));

	facts = kAliothBoard;
	facts.primary.maximumSpeed = UsbSpeed::kSuperSpeed;
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kMaximumSpeedMismatch));

	facts = kAliothBoard;
	facts.secondary.enabled = true;
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kSecondaryControllerEnabled));

	facts = kAliothBoard;
	facts.secondary.smmuStreamId = 0x21;
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kSecondarySmmuStreamMismatch));

	facts = kAliothBoard;
	facts.qmpPhyEnabled = true;
	CHECK(preflightAlioth(facts).has(PreflightIssue::kQmpPhyEnabled));

	facts = kAliothBoard;
	facts.primary.wrapper.size = 0;
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kWrapperRangeInvalid));
	CHECK(preflightAlioth(facts).has(
	    PreflightIssue::kCoreOutsideWrapper));
	printf("alioth safe preflight: PASS\n");
}

void testRegisterLayout()
{
	using namespace native_usb::dwc3;

	CHECK(kGctl == 0xc110U);
	CHECK(kGsts == 0xc118U);
	CHECK(kGsnpsid == 0xc120U);
	CHECK(gUsb2PhyCfgOffset(0) == 0xc200U);
	CHECK(gUsb2PhyCfgOffset(2) == 0xc208U);
	CHECK(gUsb3PipeCtlOffset(0) == 0xc2c0U);
	CHECK(gTxFifoSizOffset(1) == 0xc304U);
	CHECK(gRxFifoSizOffset(0) == 0xc380U);
	CHECK(kGevntadrlo == 0xc400U);
	CHECK(kGevntadrhi == 0xc404U);
	CHECK(kGevntsiz == 0xc408U);
	CHECK(kGevntcount == 0xc40cU);
	CHECK(depCmdOffset(0) == 0xc72cU);
	CHECK(depCmdOffset(1) == 0xc73cU);
	CHECK(depCmdPar2Offset(2) == 0xc740U);
	CHECK(depCmdPar1Offset(2) == 0xc744U);
	CHECK(depCmdPar0Offset(2) == 0xc748U);
	CHECK(registerRangeFitsCore(kGctl, sizeof(uint32_t)));
	CHECK(!registerRangeFitsCore(native_usb::alioth::kDwc3CoreSize, 1));
	CHECK(!registerRangeFitsCore(0, 0));
	printf("DWC3 register layout: PASS\n");
}

void testRingSizes()
{
	using namespace native_usb::dwc3;

	CHECK(sizeof(Trb) == 16);
	CHECK(sizeof(Event) == 16);
	CHECK(trbRingBytes(4) == 64);
	CHECK(trbRingBytes(0) == 0);
	CHECK(trbRingBytes(0xffffffffU) == 0);
	CHECK(eventRingBytes(256) == 4096);
	CHECK(eventRingBytes(4096) == 0);
	CHECK(eventRingBytes(0) == 0);
	CHECK(alignUp(0, 64) == 0);
	CHECK(alignUp(1, 64) == 64);
	CHECK(alignUp(64, 64) == 64);
	CHECK(alignUp(1, 3) == 0);
	CHECK(isValidRingEntryCount(4));
	CHECK(!isValidRingEntryCount(0));
	CHECK(!isValidRingEntryCount(3));
	CHECK(trbRingAllocationBytes(4) == 64);
	CHECK(eventRingAllocationBytes(256) == 4096);
	printf("DWC3 TRB/event sizes: PASS\n");
}

void testEndpointMapping()
{
	using namespace native_usb::dwc3;

	CHECK(endpointAddress(0, Direction::kOut) == 0x00);
	CHECK(endpointAddress(0, Direction::kIn) == 0x80);
	CHECK(endpointAddress(1, Direction::kIn) == 0x81);
	CHECK(endpointAddress(15, Direction::kOut) == 0x0f);
	CHECK(endpointAddress(16, Direction::kOut) == 0xff);

	CHECK(physicalEndpoint(0, Direction::kOut) == 0);
	CHECK(physicalEndpoint(0, Direction::kIn) == 1);
	CHECK(physicalEndpoint(3, Direction::kOut) == 6);
	CHECK(physicalEndpoint(15, Direction::kIn) == 31);
	CHECK(endpointNumberFromPhysical(6) == 3);
	CHECK(endpointNumberFromPhysical(31) == 15);
	CHECK(endpointNumberFromPhysical(32) == 0xff);
	CHECK(directionFromPhysical(3) == Direction::kIn);
	CHECK(directionFromPhysical(6) == Direction::kOut);

	const EndpointMap inMap = decodeEndpointAddress(0x82);
	CHECK(inMap.valid);
	CHECK(inMap.number == 2);
	CHECK(inMap.direction == Direction::kIn);
	CHECK(inMap.physical == 5);

	const EndpointMap outMap = decodeEndpointAddress(0x03);
	CHECK(outMap.valid);
	CHECK(outMap.number == 3);
	CHECK(outMap.direction == Direction::kOut);
	CHECK(outMap.physical == 6);

	const EndpointMap invalidMap = decodeEndpointAddress(0x90);
	CHECK(!invalidMap.valid);
	CHECK(invalidMap.physical == 0xff);

	const EndpointConfig config = {
		2, Direction::kIn, EndpointType::kBulk, 512, 2, 0, 0
	};
	CHECK(validEndpointConfig(config));
	CHECK((depCfgParam0(config) &
	    (kDepCfgEpTypeMask << kDepCfgEpTypeShift)) ==
	    (static_cast<uint32_t>(EndpointType::kBulk) <<
	    kDepCfgEpTypeShift));
	CHECK((depCfgParam0(config) &
	    (kDepCfgMaxPacketMask << kDepCfgMaxPacketShift)) ==
	    (512U << kDepCfgMaxPacketShift));
	CHECK((depCfgParam1(config) &
	    (kDepCfgEpNumberMask << kDepCfgEpNumberShift)) ==
	    (2U << kDepCfgEpNumberShift));
	printf("DWC3 endpoint mapping: PASS\n");
}

void testEventDecoding()
{
	using namespace native_usb::dwc3;

	const uint32_t endpointInfo = kEventEndpointBit |
	    (3U << kEventEndpointNumberShift) |
	    (1U << kEventEndpointCodeShift) |
	    (2U << kEventEndpointStatusShift) |
	    (0x1234U << kEventEndpointParamShift);
	CHECK(isEndpointEvent(endpointInfo));
	CHECK(endpointEventNumber(endpointInfo) == 3);
	CHECK(endpointEventCode(endpointInfo) == kEndpointTransferComplete);
	CHECK(endpointEventStatus(endpointInfo) == 2);
	CHECK(endpointEventParameter(endpointInfo) == 0x1234);

	const uint32_t deviceInfo = 10U << kEventDeviceCodeShift;
	CHECK(!isEndpointEvent(deviceInfo));
	CHECK(deviceEventCode(deviceInfo) == kDeviceCommandComplete);
	printf("DWC3 event decoding: PASS\n");
}

} // namespace

int main()
{
	testBoardFacts();
	testAddressRanges();
	testPreflight();
	testRegisterLayout();
	testRingSizes();
	testEndpointMapping();
	testEventDecoding();
	printf("ALL PASS\n");
	return 0;
}
