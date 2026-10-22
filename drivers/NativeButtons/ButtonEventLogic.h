/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef NATIVE_BUTTONS_BUTTON_EVENT_LOGIC_H
#define NATIVE_BUTTONS_BUTTON_EVENT_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#include "AliothButtonFacts.h"

namespace native_buttons {

struct ButtonDebounceConfig {
	uint32_t powerUs;
	uint32_t volumeUpUs;
	uint32_t volumeDownUs;
};

constexpr ButtonDebounceConfig kAliothButtonDebounceConfig = {
	kAliothPonDebounceUs,
	kAliothVolumeUpDebounceUs,
	kAliothVolumeUpDebounceUs,
};

struct ButtonTransitions {
	uint8_t pressedMask;
	uint8_t releasedMask;

	constexpr bool empty() const
	{
		return pressedMask == 0 && releasedMask == 0;
	}
};

/*
 * Pure logical state machine. Raw input is already normalled to "pressed"
 * (the GPIO6 active-low conversion belongs to the future provider).
 */
class ButtonBank {
public:
	bool init(ButtonDebounceConfig config)
	{
		if (config.powerUs == 0 ||
		    config.volumeUpUs == 0 ||
		    config.volumeDownUs == 0) {
			return false;
		}
		config_ = config;
		for (size_t i = 0; i < 3; i++) {
			channels_[i] = {};
		}
		lastUpdateUs_ = 0;
		initialized_ = true;
		return true;
	}

	ButtonTransitions update(uint8_t rawPressedMask, uint64_t nowUs)
	{
		ButtonTransitions transitions = {};
		if (!initialized_ || nowUs < lastUpdateUs_) {
			return transitions;
		}
		lastUpdateUs_ = nowUs;

		const uint32_t thresholds[3] = {
			config_.powerUs,
			config_.volumeUpUs,
			config_.volumeDownUs,
		};

		for (size_t i = 0; i < 3; i++) {
			Channel &channel = channels_[i];
			const bool rawPressed =
			    (rawPressedMask & static_cast<uint8_t>(1u << i)) != 0;

			if (channel.stablePressed == rawPressed) {
				channel.candidateValid = false;
				continue;
			}

			if (!channel.candidateValid ||
			    channel.candidatePressed != rawPressed) {
				channel.candidateValid = true;
				channel.candidatePressed = rawPressed;
				channel.candidateSinceUs = nowUs;
			}

			if (nowUs - channel.candidateSinceUs < thresholds[i]) {
				continue;
			}

			channel.stablePressed = rawPressed;
			channel.candidateValid = false;
			const uint8_t bit = static_cast<uint8_t>(1u << i);
			if (rawPressed) {
				transitions.pressedMask |= bit;
			} else {
				transitions.releasedMask |= bit;
			}
		}
		return transitions;
	}

private:
	struct Channel {
		bool stablePressed;
		bool candidateValid;
		bool candidatePressed;
		uint64_t candidateSinceUs;
	};

	ButtonDebounceConfig config_ = {};
	Channel channels_[3] = {};
	uint64_t lastUpdateUs_ = 0;
	bool initialized_ = false;
};

}  // namespace native_buttons

#endif
