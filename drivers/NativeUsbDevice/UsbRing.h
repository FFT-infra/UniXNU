/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * Host-testable USB transfer-ring mechanics adapted from
 * Allenwdk/UniXNU-uke ce8498a06d6737d0558521a8635b8f5dfb417e11.
 *
 * This file deliberately contains no board data, MMIO operations, register
 * programming, cache maintenance, interrupts, or submission doors. The
 * controller layer remains responsible for publishing ring memory in the
 * order required by the hardware.
 */
#ifndef NATIVE_USB_DEVICE_USB_RING_H
#define NATIVE_USB_DEVICE_USB_RING_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace native_usb {

/* DWC3-compatible TRB wire format. */
struct Trb {
	uint32_t bpl;
	uint32_t bph;
	uint32_t size;
	uint32_t ctrl;
} __attribute__((packed));

static_assert(sizeof(Trb) == 16, "USB TRB must be 16 bytes");

inline constexpr uint32_t kTrbHardwareOwner = 1U << 0;
inline constexpr uint32_t kTrbLast = 1U << 1;
inline constexpr uint32_t kTrbInterruptOnComplete = 1U << 11;
inline constexpr uint32_t kTrbControlNormal = 1U << 4;
inline constexpr uint32_t kTrbSizeLengthMask = 0x00ffffffU;
inline constexpr uint32_t kTrbSizeStatusMask = 0xf0000000U;

/*
 * Fill a normal transfer TRB and transfer ownership to the device by setting
 * HWO last. No cache operation or memory barrier is implied.
 */
inline void armDeviceTrb(Trb *trb, uint32_t bpl, uint32_t bph,
    uint32_t length, uint32_t control)
{
	if (trb == nullptr) {
		return;
	}
	trb->bpl = bpl;
	trb->bph = bph;
	trb->size = length & kTrbSizeLengthMask;
	trb->ctrl = control | kTrbHardwareOwner;
}

inline bool deviceOwnsTrb(const Trb &trb)
{
	return (trb.ctrl & kTrbHardwareOwner) != 0;
}

inline bool cpuOwnsTrb(const Trb &trb)
{
	return !deviceOwnsTrb(trb);
}

inline uint32_t trbResidual(const Trb &trb)
{
	return trb.size & kTrbSizeLengthMask;
}

inline uint32_t trbStatus(const Trb &trb)
{
	return (trb.size & kTrbSizeStatusMask) >> 28;
}

struct TrbRing {
	Trb *ring;
	uint32_t capacity;
	uint32_t head;
	uint32_t tail;
	uint32_t used;

	bool init(Trb *memory, uint32_t entries)
	{
		ring = nullptr;
		capacity = 0;
		head = 0;
		tail = 0;
		used = 0;
		if (memory == nullptr || entries == 0) {
			return false;
		}
		ring = memory;
		capacity = entries;
		return true;
	}

	bool empty() const { return used == 0; }
	bool full() const { return capacity != 0 && used == capacity; }
	uint32_t count() const { return used; }
	uint32_t space() const { return capacity - used; }

	Trb *producerSlot()
	{
		return ring == nullptr || full() ? nullptr : &ring[head];
	}

	void commitProducer()
	{
		if (ring != nullptr && !full()) {
			head = (head + 1) % capacity;
			used++;
		}
	}

	Trb *consumerSlot()
	{
		return ring == nullptr || empty() ? nullptr : &ring[tail];
	}

	void commitConsumer()
	{
		if (ring != nullptr && !empty()) {
			tail = (tail + 1) % capacity;
			used--;
		}
	}
};

struct ByteRing {
	uint8_t *data;
	uint32_t capacity;
	uint32_t read;
	uint32_t write;
	uint32_t used;
	uint32_t dropped;

	bool init(uint8_t *memory, uint32_t bytes)
	{
		data = nullptr;
		capacity = 0;
		read = 0;
		write = 0;
		used = 0;
		dropped = 0;
		if (memory == nullptr || bytes == 0) {
			return false;
		}
		data = memory;
		capacity = bytes;
		return true;
	}

	uint32_t available() const { return used; }
	uint32_t space() const { return capacity - used; }

	uint32_t writeBytes(const void *source, uint32_t length, bool dropOldest)
	{
		const uint8_t *src = static_cast<const uint8_t *>(source);
		uint32_t accepted = 0;

		if (src == nullptr || capacity == 0) {
			return 0;
		}
		while (accepted < length) {
			if (used == capacity) {
				if (!dropOldest) {
					break;
				}
				read = (read + 1) % capacity;
				used--;
				dropped++;
			}
			data[write] = src[accepted++];
			write = (write + 1) % capacity;
			used++;
		}
		return accepted;
	}

	uint32_t readBytes(void *destination, uint32_t length)
	{
		uint8_t *dst = static_cast<uint8_t *>(destination);
		uint32_t copied = 0;

		if (dst == nullptr || capacity == 0) {
			return 0;
		}
		while (copied < length && used != 0) {
			dst[copied++] = data[read];
			read = (read + 1) % capacity;
			used--;
		}
		return copied;
	}
};

enum class TransferTermination : uint8_t {
	kInvalid,
	kInProgress,
	kComplete,
	kShortPacket
};

struct TransferProgress {
	uint32_t transferred;
	TransferTermination termination;

	bool terminal() const
	{
		return termination == TransferTermination::kComplete ||
		    termination == TransferTermination::kShortPacket;
	}
};

/*
 * Classify a completed device-controller observation from the requested
 * length, residual in the TRB, and endpoint maximum packet size. A short
 * packet terminates the transfer even when other TRBs remain in the ring.
 */
inline TransferProgress classifyTransfer(uint32_t requested,
    uint32_t residual, uint32_t maxPacketSize)
{
	if (maxPacketSize == 0 || requested > kTrbSizeLengthMask ||
	    residual > requested) {
		return {0, TransferTermination::kInvalid};
	}

	const uint32_t transferred = requested - residual;
	if (transferred == requested) {
		return {transferred, TransferTermination::kComplete};
	}
	if (transferred == 0 || (transferred % maxPacketSize) != 0) {
		return {transferred, TransferTermination::kShortPacket};
	}
	return {transferred, TransferTermination::kInProgress};
}

} // namespace native_usb

#endif /* NATIVE_USB_DEVICE_USB_RING_H */
