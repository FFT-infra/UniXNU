/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only checks for the USB descriptor and ring mechanics. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "UsbDescriptors.h"
#include "UsbRing.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
		exit(1); \
	} \
} while (0)

namespace {

void testDescriptorLengths()
{
	native_usb::Descriptors descriptors;
	descriptors.build();

	CHECK(sizeof(descriptors.device) == 18);
	CHECK(descriptors.device[0] == 18);
	CHECK(descriptors.device[1] == 1);
	CHECK(descriptors.device[7] == 64);
	CHECK(descriptors.device[8] == 0x25 && descriptors.device[9] == 0x05);
	CHECK(descriptors.device[10] == 0xa7 && descriptors.device[11] == 0xa4);
	CHECK(descriptors.device[17] == 1);

	CHECK(descriptors.configLength == 75);
	CHECK(descriptors.configLength <= sizeof(descriptors.config));
	CHECK(descriptors.config[0] == 9 && descriptors.config[1] == 2);
	CHECK(descriptors.config[2] == 75 && descriptors.config[3] == 0);
	CHECK(descriptors.config[4] == 2);

	unsigned descriptorCount = 0;
	unsigned interfaceCount = 0;
	unsigned endpointCount = 0;
	bool sawIad = false;
	for (size_t offset = 0; offset < descriptors.configLength;) {
		CHECK(offset + 2 <= descriptors.configLength);
		const uint8_t length = descriptors.config[offset];
		const uint8_t type = descriptors.config[offset + 1];
		CHECK(length >= 2);
		CHECK(offset + length <= descriptors.configLength);
		descriptorCount++;
		if (type == 0x0b) {
			sawIad = true;
			CHECK(length == 8);
		} else if (type == 4) {
			interfaceCount++;
			CHECK(length == 9);
		} else if (type == 5) {
			endpointCount++;
			CHECK(length == 7);
		} else if (type == 0x24) {
			CHECK(length == 4 || length == 5);
		}
		offset += length;
	}
	CHECK(descriptorCount == 11);
	CHECK(interfaceCount == 2);
	CHECK(endpointCount == 3);
	CHECK(sawIad);

	CHECK(descriptors.stringLangLength == 4);
	CHECK(descriptors.stringLang[0] == 4);
	CHECK(descriptors.stringLang[1] == 3);
	CHECK(descriptors.stringLang[2] == 0x09);
	CHECK(descriptors.stringLang[3] == 0x04);

	const uint8_t *strings[] = {
		descriptors.stringManufacturer,
		descriptors.stringProduct,
		descriptors.stringSerial
	};
	const uint8_t lengths[] = {
		descriptors.stringManufacturerLength,
		descriptors.stringProductLength,
		descriptors.stringSerialLength
	};
	const size_t capacities[] = {
		sizeof(descriptors.stringManufacturer),
		sizeof(descriptors.stringProduct),
		sizeof(descriptors.stringSerial)
	};
	for (size_t i = 0; i < 3; i++) {
		CHECK(lengths[i] >= 2);
		CHECK((lengths[i] & 1U) == 0);
		CHECK(lengths[i] <= capacities[i]);
		CHECK(strings[i][0] == lengths[i]);
		CHECK(strings[i][1] == 3);
	}

	printf("descriptor lengths: PASS\n");
}

void testEndpointDirections()
{
	native_usb::Descriptors descriptors;
	descriptors.build();

	unsigned endpointIndex = 0;
	for (size_t offset = 0; offset < descriptors.configLength;) {
		const uint8_t length = descriptors.config[offset];
		const uint8_t type = descriptors.config[offset + 1];
		if (type != 5) {
			offset += length;
			continue;
		}

		const uint8_t address = descriptors.config[offset + 2];
		const uint8_t attributes = descriptors.config[offset + 3];
		const uint16_t maxPacket = static_cast<uint16_t>(
		    descriptors.config[offset + 4] |
		    (descriptors.config[offset + 5] << 8));
		const bool directionIn = (address & 0x80U) != 0;

		switch (endpointIndex) {
		case 0:
			CHECK(address == 0x81 && directionIn);
			CHECK((attributes & 0x03U) == 0x03U);
			CHECK(maxPacket == 16);
			break;
		case 1:
			CHECK(address == 0x82 && directionIn);
			CHECK((attributes & 0x03U) == 0x02U);
			CHECK(maxPacket == 512);
			break;
		case 2:
			CHECK(address == 0x03 && !directionIn);
			CHECK((attributes & 0x03U) == 0x02U);
			CHECK(maxPacket == 512);
			break;
		default:
			CHECK(false);
		}
		endpointIndex++;
		offset += length;
	}
	CHECK(endpointIndex == 3);
	printf("endpoint directions: PASS\n");
}

void testTrbRingWrap()
{
	native_usb::Trb memory[4] = {};
	native_usb::TrbRing ring;

	CHECK(!ring.init(nullptr, 4));
	CHECK(ring.empty() && !ring.full() && ring.producerSlot() == nullptr);
	CHECK(!ring.init(memory, 0));
	CHECK(ring.init(memory, 4));
	CHECK(ring.empty() && ring.space() == 4);

	for (uint32_t i = 0; i < 3; i++) {
		native_usb::Trb *slot = ring.producerSlot();
		CHECK(slot == &memory[i]);
		slot->ctrl = 0x100U + i;
		ring.commitProducer();
	}
	CHECK(ring.count() == 3 && ring.head == 3 && ring.tail == 0);
	for (uint32_t i = 0; i < 3; i++) {
		native_usb::Trb *slot = ring.consumerSlot();
		CHECK(slot == &memory[i] && slot->ctrl == 0x100U + i);
		ring.commitConsumer();
	}
	CHECK(ring.empty() && ring.head == 3 && ring.tail == 3);

	for (uint32_t i = 0; i < 4; i++) {
		native_usb::Trb *slot = ring.producerSlot();
		native_usb::Trb *expected[] = {
			&memory[3], &memory[0], &memory[1], &memory[2]
		};
		CHECK(slot == expected[i]);
		slot->ctrl = 0x200U + i;
		ring.commitProducer();
	}
	CHECK(ring.full() && ring.producerSlot() == nullptr);
	CHECK(ring.head == 3 && ring.tail == 3);

	const uint32_t expectedControls[] = {0x200U, 0x201U, 0x202U, 0x203U};
	for (uint32_t i = 0; i < 4; i++) {
		native_usb::Trb *slot = ring.consumerSlot();
		CHECK(slot != nullptr && slot->ctrl == expectedControls[i]);
		ring.commitConsumer();
	}
	CHECK(ring.empty() && ring.consumerSlot() == nullptr);
	printf("TRB ring wrap: PASS\n");
}

void testTrbOwnership()
{
	native_usb::Trb trb = {};
	const uint32_t control =
	    native_usb::kTrbControlNormal |
	    native_usb::kTrbLast |
	    native_usb::kTrbInterruptOnComplete;

	native_usb::armDeviceTrb(&trb, 0x12345000U, 0x2U, 512, control);
	CHECK(trb.bpl == 0x12345000U);
	CHECK(trb.bph == 0x2U);
	CHECK(trb.size == 512);
	CHECK(trb.ctrl == (control | native_usb::kTrbHardwareOwner));
	CHECK(native_usb::deviceOwnsTrb(trb));
	CHECK(!native_usb::cpuOwnsTrb(trb));
	CHECK(native_usb::trbResidual(trb) == 512);

	/* Simulate the device retiring the TRB without a CPU-side MMIO helper. */
	trb.size = (trb.size & ~native_usb::kTrbSizeLengthMask) | 500U;
	trb.ctrl &= ~native_usb::kTrbHardwareOwner;

	CHECK(native_usb::cpuOwnsTrb(trb));
	CHECK(!native_usb::deviceOwnsTrb(trb));
	CHECK(native_usb::trbResidual(trb) == 500);
	CHECK(native_usb::trbStatus(trb) == 0);
	printf("TRB ownership: PASS\n");
}

void testShortPacketTermination()
{
	CHECK(native_usb::classifyTransfer(512, 0, 512).terminal());
	CHECK(native_usb::classifyTransfer(512, 0, 512).transferred == 512);

	native_usb::TransferProgress shortTransfer =
	    native_usb::classifyTransfer(512, 500, 512);
	CHECK(shortTransfer.transferred == 12);
	CHECK(shortTransfer.termination ==
	    native_usb::TransferTermination::kShortPacket);
	CHECK(shortTransfer.terminal());

	native_usb::TransferProgress midPacket =
	    native_usb::classifyTransfer(1024, 512, 512);
	CHECK(midPacket.transferred == 512);
	CHECK(midPacket.termination ==
	    native_usb::TransferTermination::kInProgress);
	CHECK(!midPacket.terminal());

	native_usb::TransferProgress zeroLength =
	    native_usb::classifyTransfer(512, 512, 512);
	CHECK(zeroLength.transferred == 0);
	CHECK(zeroLength.termination ==
	    native_usb::TransferTermination::kShortPacket);
	CHECK(zeroLength.terminal());

	CHECK(native_usb::classifyTransfer(0, 0, 512).termination ==
	    native_usb::TransferTermination::kComplete);
	CHECK(native_usb::classifyTransfer(512, 513, 512).termination ==
	    native_usb::TransferTermination::kInvalid);
	CHECK(native_usb::classifyTransfer(512, 0, 0).termination ==
	    native_usb::TransferTermination::kInvalid);
	printf("short-packet termination: PASS\n");
}

void testByteRingWrap()
{
	uint8_t memory[8] = {};
	native_usb::ByteRing ring;

	CHECK(!ring.init(nullptr, 8));
	CHECK(!ring.init(memory, 0));
	CHECK(ring.init(memory, sizeof(memory)));
	CHECK(ring.writeBytes("abcdef", 6, false) == 6);

	uint8_t output[8] = {};
	CHECK(ring.readBytes(output, 4) == 4);
	CHECK(memcmp(output, "abcd", 4) == 0);
	CHECK(ring.writeBytes("ghijkl", 6, false) == 6);
	CHECK(ring.available() == 8);
	CHECK(ring.writeBytes("x", 1, false) == 0);

	memset(output, 0, sizeof(output));
	CHECK(ring.readBytes(output, sizeof(output)) == 8);
	CHECK(memcmp(output, "efghijkl", 8) == 0);
	CHECK(ring.available() == 0);
	printf("byte ring wrap: PASS\n");
}

} // namespace

int main()
{
	testDescriptorLengths();
	testEndpointDirections();
	testTrbRingWrap();
	testTrbOwnership();
	testShortPacketTermination();
	testByteRingWrap();
	printf("ALL PASS\n");
	return 0;
}
