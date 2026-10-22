/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host-testable CDC-ACM descriptor construction adapted from
 * Allenwdk/UniXNU-uke ce8498a06d6737d0558521a8635b8f5dfb417e11.
 *
 * No board data, MMIO operations, endpoint programming, or kernel services
 * are present here. The controller layer copies these bytes to the host.
 */
#ifndef NATIVE_USB_DEVICE_USB_DESCRIPTORS_H
#define NATIVE_USB_DEVICE_USB_DESCRIPTORS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace native_usb {

inline constexpr uint16_t kDefaultDescriptorVendorId = 0x0525U;
inline constexpr uint16_t kDefaultDescriptorProductId = 0xa4a7U;
inline constexpr size_t kDeviceDescriptorStorage = 18;
inline constexpr size_t kConfigDescriptorStorage = 80;
inline constexpr size_t kLanguageDescriptorStorage = 4;
inline constexpr size_t kManufacturerDescriptorStorage = 32;
inline constexpr size_t kProductDescriptorStorage = 40;
inline constexpr size_t kSerialDescriptorStorage = 40;

namespace descriptor_detail {

inline void put8(uint8_t *&cursor, uint8_t value)
{
	*cursor++ = value;
}

inline void put16(uint8_t *&cursor, uint16_t value)
{
	*cursor++ = static_cast<uint8_t>(value & 0xffU);
	*cursor++ = static_cast<uint8_t>(value >> 8);
}

inline uint8_t makeString(uint8_t *output, size_t capacity,
    const char *ascii)
{
	size_t characters = 0;

	while (ascii != nullptr && ascii[characters] != '\0' &&
	    (2 * characters + 4) <= capacity) {
		characters++;
	}
	if (characters == 0) {
		if (capacity >= 2) {
			output[0] = 2;
			output[1] = 3;
			return 2;
		}
		return 0;
	}

	const size_t length = 2 + 2 * characters;
	output[0] = static_cast<uint8_t>(length);
	output[1] = 3;
	for (size_t i = 0; i < characters; i++) {
		output[2 + 2 * i] = static_cast<uint8_t>(ascii[i]);
		output[3 + 2 * i] = 0;
	}
	return static_cast<uint8_t>(length);
}

} // namespace descriptor_detail

struct Descriptors {
	uint8_t device[kDeviceDescriptorStorage];
	uint8_t config[kConfigDescriptorStorage];
	uint16_t configLength;
	uint8_t stringLang[kLanguageDescriptorStorage];
	uint8_t stringManufacturer[kManufacturerDescriptorStorage];
	uint8_t stringProduct[kProductDescriptorStorage];
	uint8_t stringSerial[kSerialDescriptorStorage];
	uint8_t stringLangLength;
	uint8_t stringManufacturerLength;
	uint8_t stringProductLength;
	uint8_t stringSerialLength;

	void build(uint16_t vid = kDefaultDescriptorVendorId,
	    uint16_t pid = kDefaultDescriptorProductId);
};

inline void Descriptors::build(uint16_t vid, uint16_t pid)
{
	using namespace descriptor_detail;

	memset(this, 0, sizeof(*this));

	uint8_t *deviceCursor = device;
	put8(deviceCursor, 18);                 // bLength
	put8(deviceCursor, 1);                  // DEVICE
	put16(deviceCursor, 0x0200);            // bcdUSB 2.00
	put8(deviceCursor, 0x02);               // bDeviceClass = CDC
	put8(deviceCursor, 0x00);               // bDeviceSubClass
	put8(deviceCursor, 0x00);               // bDeviceProtocol
	put8(deviceCursor, 64);                 // bMaxPacketSize0
	put16(deviceCursor, vid);
	put16(deviceCursor, pid);
	put16(deviceCursor, 0x0100);            // bcdDevice
	put8(deviceCursor, 1);                  // iManufacturer
	put8(deviceCursor, 2);                  // iProduct
	put8(deviceCursor, 3);                  // iSerialNumber
	put8(deviceCursor, 1);                  // bNumConfigurations

	uint8_t *configCursor = config;
	put8(configCursor, 9);
	put8(configCursor, 2);
	uint8_t *totalLength = configCursor;
	put16(configCursor, 0);
	put8(configCursor, 2);                  // bNumInterfaces
	put8(configCursor, 1);                  // bConfigurationValue
	put8(configCursor, 0);                  // iConfiguration
	put8(configCursor, 0x80);               // bus powered
	put8(configCursor, 50);                 // 100 mA

	/* Interface association: CDC control plus data. */
	put8(configCursor, 8);
	put8(configCursor, 0x0b);               // IAD
	put8(configCursor, 0);
	put8(configCursor, 2);
	put8(configCursor, 0x02);
	put8(configCursor, 0x02);
	put8(configCursor, 0x01);
	put8(configCursor, 0);

	/* CDC control interface. */
	put8(configCursor, 9);
	put8(configCursor, 4);
	put8(configCursor, 0);
	put8(configCursor, 0);
	put8(configCursor, 1);                  // one notification endpoint
	put8(configCursor, 0x02);
	put8(configCursor, 0x02);
	put8(configCursor, 0x01);
	put8(configCursor, 0);

	/* CDC functional descriptors. */
	put8(configCursor, 5); put8(configCursor, 0x24);
	put8(configCursor, 0x00); put8(configCursor, 0x10);
	put8(configCursor, 0x01);
	put8(configCursor, 5); put8(configCursor, 0x24);
	put8(configCursor, 0x01); put8(configCursor, 0x00);
	put8(configCursor, 0x01);
	put8(configCursor, 4); put8(configCursor, 0x24);
	put8(configCursor, 0x02); put8(configCursor, 0x02);
	put8(configCursor, 5); put8(configCursor, 0x24);
	put8(configCursor, 0x06); put8(configCursor, 0x00);
	put8(configCursor, 0x01);

	/* Notification endpoint: interrupt IN, 16 bytes, 16 ms. */
	put8(configCursor, 7); put8(configCursor, 5);
	put8(configCursor, 0x81); put8(configCursor, 0x03);
	put16(configCursor, 16); put8(configCursor, 16);

	/* CDC data interface. */
	put8(configCursor, 9);
	put8(configCursor, 4);
	put8(configCursor, 1);
	put8(configCursor, 0);
	put8(configCursor, 2);                  // two bulk endpoints
	put8(configCursor, 0x0a);
	put8(configCursor, 0x00);
	put8(configCursor, 0x00);
	put8(configCursor, 0);

	/* Bulk IN and bulk OUT, high-speed 512-byte packets. */
	put8(configCursor, 7); put8(configCursor, 5);
	put8(configCursor, 0x82); put8(configCursor, 0x02);
	put16(configCursor, 512); put8(configCursor, 0);
	put8(configCursor, 7); put8(configCursor, 5);
	put8(configCursor, 0x03); put8(configCursor, 0x02);
	put16(configCursor, 512); put8(configCursor, 0);

	configLength = static_cast<uint16_t>(configCursor - config);
	totalLength[0] = static_cast<uint8_t>(configLength & 0xffU);
	totalLength[1] = static_cast<uint8_t>(configLength >> 8);

	stringLang[0] = 4;
	stringLang[1] = 3;
	stringLang[2] = 0x09;
	stringLang[3] = 0x04;
	stringLangLength = 4;
	stringManufacturerLength = makeString(stringManufacturer,
	    sizeof(stringManufacturer), "UniXNU");
	stringProductLength = makeString(stringProduct, sizeof(stringProduct),
	    "UniXNU USB CDC");
	stringSerialLength = makeString(stringSerial, sizeof(stringSerial),
	    "ALIOTH0001");
}

} // namespace native_usb

#endif /* NATIVE_USB_DEVICE_USB_DESCRIPTORS_H */
