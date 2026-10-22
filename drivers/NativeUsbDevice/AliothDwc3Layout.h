/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host-testable DWC3 register and wire layout.
 *
 * Register offsets, bitfields, event codes, and packed wire shapes are
 * transcribed from Allenwdk/UniXNU-uke commit
 * ce8498a06d6737d0558521a8635b8f5dfb417e11,
 * drivers/NativeUsbSerial/UkeUsbDwc3.h. Only auditable constants and pure
 * arithmetic are kept here. No MMIO, cache maintenance, interrupt, clock,
 * PHY, PDC, GDSC, or kernel-service operation is present.
 */
#ifndef NATIVE_USB_DEVICE_ALIOTH_DWC3_LAYOUT_H
#define NATIVE_USB_DEVICE_ALIOTH_DWC3_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "AliothUsbBoard.h"
#include "UsbRing.h"

namespace native_usb {
namespace dwc3 {

using Trb = native_usb::Trb;

struct Event {
	uint32_t info;
	uint32_t param;
	uint32_t status;
	uint32_t reserved;
} __attribute__((packed));

static_assert(sizeof(Trb) == 16, "DWC3 TRB must be 16 bytes");
static_assert(sizeof(Event) == 16, "DWC3 event must be 16 bytes");

/* Global register offsets, relative to the DWC3 core base. */
inline constexpr uint32_t kGctl = 0xc110U;
inline constexpr uint32_t kGsts = 0xc118U;
inline constexpr uint32_t kGsnpsid = 0xc120U;
inline constexpr uint32_t kGuid = 0xc128U;
inline constexpr uint32_t kGhwparams0 = 0xc140U;
inline constexpr uint32_t kGhwparams1 = 0xc144U;
inline constexpr uint32_t kGhwparams2 = 0xc148U;
inline constexpr uint32_t kGhwparams3 = 0xc14cU;
inline constexpr uint32_t kGusb2phycfgBase = 0xc200U;
inline constexpr uint32_t kGusb3pipectlBase = 0xc2c0U;
inline constexpr uint32_t kGtxfifosizBase = 0xc300U;
inline constexpr uint32_t kGrxfifosizBase = 0xc380U;
inline constexpr uint32_t kGevntadrlo = 0xc400U;
inline constexpr uint32_t kGevntadrhi = 0xc404U;
inline constexpr uint32_t kGevntsiz = 0xc408U;
inline constexpr uint32_t kGevntcount = 0xc40cU;
inline constexpr uint32_t kDcfg = 0xc700U;
inline constexpr uint32_t kDctl = 0xc704U;
inline constexpr uint32_t kDevten = 0xc708U;
inline constexpr uint32_t kDsts = 0xc70cU;
inline constexpr uint32_t kDepcmdpar2Base = 0xc720U;
inline constexpr uint32_t kDepcmdpar1Base = 0xc724U;
inline constexpr uint32_t kDepcmdpar0Base = 0xc728U;
inline constexpr uint32_t kDepcmdBase = 0xc72cU;

constexpr uint32_t gUsb2PhyCfgOffset(uint32_t instance)
{
	return kGusb2phycfgBase + 4U * instance;
}

constexpr uint32_t gUsb3PipeCtlOffset(uint32_t instance)
{
	return kGusb3pipectlBase + 4U * instance;
}

constexpr uint32_t gTxFifoSizOffset(uint32_t instance)
{
	return kGtxfifosizBase + 4U * instance;
}

constexpr uint32_t gRxFifoSizOffset(uint32_t instance)
{
	return kGrxfifosizBase + 4U * instance;
}

constexpr uint32_t depCmdPar2Offset(uint32_t physicalEndpoint)
{
	return kDepcmdpar2Base + 16U * physicalEndpoint;
}

constexpr uint32_t depCmdPar1Offset(uint32_t physicalEndpoint)
{
	return kDepcmdpar1Base + 16U * physicalEndpoint;
}

constexpr uint32_t depCmdPar0Offset(uint32_t physicalEndpoint)
{
	return kDepcmdpar0Base + 16U * physicalEndpoint;
}

constexpr uint32_t depCmdOffset(uint32_t physicalEndpoint)
{
	return kDepcmdBase + 16U * physicalEndpoint;
}

constexpr bool registerRangeFitsCore(uint64_t offset, uint64_t width)
{
	return width != 0 && offset <= alioth::kDwc3CoreSize &&
	    width <= alioth::kDwc3CoreSize - offset;
}

inline constexpr uint32_t kMaxPhysicalEndpoints = 32;
inline constexpr uint32_t kEventRingSizeMask = 0x0000ffffU;
inline constexpr uint32_t kEventRingDisable = 1U << 31;
inline constexpr uint32_t kEventCountMask = 0x0000ffffU;
inline constexpr uint32_t kRingAlignment = 64;

constexpr uint32_t elementBytes(uint32_t entries, uint32_t elementSize)
{
	if (entries == 0 || elementSize == 0 ||
	    entries > (~static_cast<uint32_t>(0)) / elementSize) {
		return 0;
	}
	return entries * elementSize;
}

constexpr uint32_t trbRingBytes(uint32_t entries)
{
	return elementBytes(entries, static_cast<uint32_t>(sizeof(Trb)));
}

constexpr uint32_t eventRingBytes(uint32_t entries)
{
	const uint32_t bytes = elementBytes(entries,
	    static_cast<uint32_t>(sizeof(Event)));
	return bytes <= kEventRingSizeMask ? bytes : 0;
}

constexpr bool isPowerOfTwo(uint32_t value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

constexpr uint32_t alignUp(uint32_t value, uint32_t alignment)
{
	if (alignment == 0 || !isPowerOfTwo(alignment) ||
	    value > (~static_cast<uint32_t>(0)) - (alignment - 1)) {
		return 0;
	}
	return (value + alignment - 1) & ~(alignment - 1);
}

constexpr uint32_t trbRingAllocationBytes(uint32_t entries)
{
	return alignUp(trbRingBytes(entries), kRingAlignment);
}

constexpr uint32_t eventRingAllocationBytes(uint32_t entries)
{
	return alignUp(eventRingBytes(entries), kRingAlignment);
}

constexpr bool isValidRingEntryCount(uint32_t entries)
{
	return entries >= 2 && isPowerOfTwo(entries);
}

/* GCTL. */
inline constexpr uint32_t kGctlDisableClockGating = 1U << 0;

/* GUSB2PHYCFG. */
inline constexpr uint32_t kGusb2phycfgSusPhy = 1U << 6;
inline constexpr uint32_t kGusb2phycfgEnblSlpm = 1U << 8;

/* GUSB3PIPECTL. */
inline constexpr uint32_t kGusb3pipectlSusPhy = 1U << 17;
inline constexpr uint32_t kGusb3pipectlDep1p2p3En = 1U << 19;

/* DCFG/DCTL/DSTS. */
inline constexpr uint32_t kDcfgDevSpdHs = 0U;
inline constexpr uint32_t kDcfgDevSpdMask = 0x7U;
inline constexpr uint32_t kDcfgDevAddrShift = 3;
inline constexpr uint32_t kDcfgDevAddrMask = 0x7fU;
inline constexpr uint32_t kDctlRunStop = 1U << 31;
inline constexpr uint32_t kDctlCsfReset = 1U << 30;
inline constexpr uint32_t kDstsDevCtrlHlt = 1U << 22;
inline constexpr uint32_t kDstsConnectSpdMask = 0x7U;

/* DEVTEN. */
inline constexpr uint32_t kDevtenDisconnect = 1U << 0;
inline constexpr uint32_t kDevtenUsbReset = 1U << 1;
inline constexpr uint32_t kDevtenConnectDone = 1U << 2;
inline constexpr uint32_t kDevtenUlstChng = 1U << 3;
inline constexpr uint32_t kDevtenWakeup = 1U << 4;
inline constexpr uint32_t kDevtenSof = 1U << 7;
inline constexpr uint32_t kDevtenErrorTic = 1U << 9;
inline constexpr uint32_t kDevtenCmdCmplt = 1U << 10;

/* Event info layout. */
inline constexpr uint32_t kEventEndpointBit = 1U << 0;
inline constexpr uint32_t kEventDeviceCodeShift = 1;
inline constexpr uint32_t kEventDeviceCodeMask = 0x7fU;
inline constexpr uint32_t kEventEndpointNumberShift = 1;
inline constexpr uint32_t kEventEndpointNumberMask = 0x1fU;
inline constexpr uint32_t kEventEndpointCodeShift = 6;
inline constexpr uint32_t kEventEndpointCodeMask = 0x0fU;
inline constexpr uint32_t kEventEndpointStatusShift = 10;
inline constexpr uint32_t kEventEndpointStatusMask = 0x0fU;
inline constexpr uint32_t kEventEndpointParamShift = 14;
inline constexpr uint32_t kEventEndpointParamMask = 0xffffU;

constexpr bool isEndpointEvent(uint32_t info)
{
	return (info & kEventEndpointBit) != 0;
}

constexpr uint8_t deviceEventCode(uint32_t info)
{
	return static_cast<uint8_t>(
	    (info >> kEventDeviceCodeShift) & kEventDeviceCodeMask);
}

constexpr uint8_t endpointEventNumber(uint32_t info)
{
	return static_cast<uint8_t>(
	    (info >> kEventEndpointNumberShift) & kEventEndpointNumberMask);
}

constexpr uint8_t endpointEventCode(uint32_t info)
{
	return static_cast<uint8_t>(
	    (info >> kEventEndpointCodeShift) & kEventEndpointCodeMask);
}

constexpr uint8_t endpointEventStatus(uint32_t info)
{
	return static_cast<uint8_t>(
	    (info >> kEventEndpointStatusShift) & kEventEndpointStatusMask);
}

constexpr uint16_t endpointEventParameter(uint32_t info)
{
	return static_cast<uint16_t>(
	    (info >> kEventEndpointParamShift) & kEventEndpointParamMask);
}

/* Device event codes. */
inline constexpr uint8_t kDeviceDisconnect = 0;
inline constexpr uint8_t kDeviceReset = 1;
inline constexpr uint8_t kDeviceConnectDone = 2;
inline constexpr uint8_t kDeviceLinkStatus = 3;
inline constexpr uint8_t kDeviceWakeup = 4;
inline constexpr uint8_t kDeviceHibernationRequest = 5;
inline constexpr uint8_t kDeviceSuspend = 6;
inline constexpr uint8_t kDeviceSof = 7;
inline constexpr uint8_t kDeviceErraticError = 9;
inline constexpr uint8_t kDeviceCommandComplete = 10;
inline constexpr uint8_t kDeviceOverflow = 11;

/* Endpoint event codes. */
inline constexpr uint8_t kEndpointTransferComplete = 1;
inline constexpr uint8_t kEndpointTransferInProgress = 2;
inline constexpr uint8_t kEndpointTransferNotReady = 3;
inline constexpr uint8_t kEndpointCommandComplete = 7;

/* TRB control fields. The record itself is shared with UsbRing.h. */
inline constexpr uint32_t kTrbHardwareOwner = native_usb::kTrbHardwareOwner;
inline constexpr uint32_t kTrbLast = native_usb::kTrbLast;
inline constexpr uint32_t kTrbChain = 1U << 2;
inline constexpr uint32_t kTrbContinueOnShortPacket = 1U << 3;
inline constexpr uint32_t kTrbControlTypeShift = 4;
inline constexpr uint32_t kTrbControlTypeMask = 0x3fU;
inline constexpr uint32_t kTrbInterruptOnComplete =
    native_usb::kTrbInterruptOnComplete;
inline constexpr uint32_t kTrbSizeLengthMask =
    native_usb::kTrbSizeLengthMask;
inline constexpr uint32_t kTrbSizeStatusMask =
    native_usb::kTrbSizeStatusMask;

/* TRB control types. */
inline constexpr uint32_t kTrbNormal = 1U << kTrbControlTypeShift;
inline constexpr uint32_t kTrbControlSetup = 2U << kTrbControlTypeShift;
inline constexpr uint32_t kTrbControlStatus2 = 3U << kTrbControlTypeShift;
inline constexpr uint32_t kTrbControlStatus3 = 4U << kTrbControlTypeShift;
inline constexpr uint32_t kTrbControlData = 5U << kTrbControlTypeShift;
inline constexpr uint32_t kTrbLink = 8U << kTrbControlTypeShift;

/* Endpoint commands and result fields. */
inline constexpr uint32_t kDepCmdSetEpConfig = 0x01U;
inline constexpr uint32_t kDepCmdSetTransferResource = 0x02U;
inline constexpr uint32_t kDepCmdSetStall = 0x04U;
inline constexpr uint32_t kDepCmdClearStall = 0x05U;
inline constexpr uint32_t kDepCmdStartTransfer = 0x06U;
inline constexpr uint32_t kDepCmdUpdateTransfer = 0x07U;
inline constexpr uint32_t kDepCmdEndTransfer = 0x08U;
inline constexpr uint32_t kDepCmdStartConfig = 0x09U;
inline constexpr uint32_t kDepCmdActive = 1U << 10;
inline constexpr uint32_t kDepCmdStatusShift = 12;
inline constexpr uint32_t kDepCmdStatusMask = 0x0fU;
inline constexpr uint32_t kDepCmdParamShift = 16;
inline constexpr uint32_t kDepCmdResourceIndexShift = 16;
inline constexpr uint32_t kDepCmdResourceIndexMask = 0x7fU;

/* Endpoint configuration fields. */
inline constexpr uint32_t kDepCfgEpTypeShift = 1;
inline constexpr uint32_t kDepCfgEpTypeMask = 0x3U;
inline constexpr uint32_t kDepCfgMaxPacketShift = 3;
inline constexpr uint32_t kDepCfgMaxPacketMask = 0x7ffU;
inline constexpr uint32_t kDepCfgFifoNumberShift = 17;
inline constexpr uint32_t kDepCfgFifoNumberMask = 0x1fU;
inline constexpr uint32_t kDepCfgBurstSizeShift = 22;
inline constexpr uint32_t kDepCfgBurstSizeMask = 0xfU;
inline constexpr uint32_t kDepCfgActionShift = 30;
inline constexpr uint32_t kDepCfgActionMask = 0x3U;
inline constexpr uint32_t kDepCfgXferCompleteEn = 1U << 8;
inline constexpr uint32_t kDepCfgXferInProgressEn = 1U << 9;
inline constexpr uint32_t kDepCfgXferNotReadyEn = 1U << 10;
inline constexpr uint32_t kDepCfgBintervalShift = 16;
inline constexpr uint32_t kDepCfgBintervalMask = 0xffU;
inline constexpr uint32_t kDepCfgEpNumberShift = 25;
inline constexpr uint32_t kDepCfgEpNumberMask = 0x1fU;

enum class EndpointType : uint8_t {
	kControl = 0,
	kIsochronous = 1,
	kBulk = 2,
	kInterrupt = 3,
};

enum class Direction : uint8_t {
	kOut = 0,
	kIn = 1,
};

constexpr bool validEndpointNumber(uint8_t number)
{
	return number <= 15;
}

constexpr bool validPhysicalEndpoint(uint32_t physical)
{
	return physical < kMaxPhysicalEndpoints;
}

constexpr uint8_t endpointAddress(uint8_t number, Direction direction)
{
	if (!validEndpointNumber(number)) {
		return 0xffU;
	}
	return static_cast<uint8_t>(
	    number | (direction == Direction::kIn ? 0x80U : 0x00U));
}

constexpr uint8_t physicalEndpoint(uint8_t number, Direction direction)
{
	if (!validEndpointNumber(number)) {
		return 0xffU;
	}
	return static_cast<uint8_t>(
	    2U * number + (direction == Direction::kIn ? 1U : 0U));
}

constexpr uint8_t endpointNumberFromPhysical(uint32_t physical)
{
	return validPhysicalEndpoint(physical) ?
	    static_cast<uint8_t>(physical >> 1) : 0xffU;
}

constexpr Direction directionFromPhysical(uint32_t physical)
{
	return (physical & 1U) != 0 ? Direction::kIn : Direction::kOut;
}

struct EndpointMap {
	bool valid;
	uint8_t number;
	Direction direction;
	uint8_t physical;
};

constexpr EndpointMap decodeEndpointAddress(uint8_t address)
{
	/* Bits 4..6 are reserved in a USB endpoint address. */
	if ((address & 0x70U) != 0) {
		return { false, 0, Direction::kOut, 0xffU };
	}
	const uint8_t number = address & 0x0fU;
	const Direction direction = (address & 0x80U) != 0 ?
	    Direction::kIn : Direction::kOut;
	return { true, number, direction, physicalEndpoint(number, direction) };
}

struct EndpointConfig {
	uint8_t number;
	Direction direction;
	EndpointType type;
	uint16_t maxPacketSize;
	uint8_t fifoNumber;
	uint8_t burstSize;
	uint8_t binterval;
};

constexpr bool validEndpointConfig(const EndpointConfig &config)
{
	return validEndpointNumber(config.number) &&
	    config.type <= EndpointType::kInterrupt &&
	    config.maxPacketSize <= kDepCfgMaxPacketMask &&
	    config.fifoNumber <= kDepCfgFifoNumberMask &&
	    config.burstSize <= kDepCfgBurstSizeMask;
}

constexpr uint32_t depCfgParam0(const EndpointConfig &config)
{
	return (static_cast<uint32_t>(config.type) << kDepCfgEpTypeShift) |
	    (static_cast<uint32_t>(config.maxPacketSize) <<
	    kDepCfgMaxPacketShift) |
	    (static_cast<uint32_t>(config.fifoNumber) <<
	    kDepCfgFifoNumberShift) |
	    (static_cast<uint32_t>(config.burstSize) <<
	    kDepCfgBurstSizeShift);
}

constexpr uint32_t depCfgParam1(const EndpointConfig &config)
{
	return (static_cast<uint32_t>(config.number) <<
	    kDepCfgEpNumberShift) |
	    (static_cast<uint32_t>(config.binterval) <<
	    kDepCfgBintervalShift) |
	    kDepCfgXferCompleteEn | kDepCfgXferInProgressEn;
}

} // namespace dwc3
} // namespace native_usb

#endif /* NATIVE_USB_DEVICE_ALIOTH_DWC3_LAYOUT_H */
