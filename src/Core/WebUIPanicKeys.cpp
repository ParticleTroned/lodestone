// WebUIPanicKeys.cpp
// Lodestone - Shared SKSE framework
//
// See WebUIPanicKeys.h. Moved out of MeridianUIBackend.cpp in 1.27.0 without a
// behavior change.

#include "WebUIPanicKeys.h"

#include "Config.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace Lodestone::Core::WebUIPanicKeys
{
	namespace
	{
		// Ctrl+Backspace by default: unbound in vanilla Skyrim, reachable with
		// one hand, and not a chord a page is likely to want for itself.
		Chord g_chord{ RE::BSKeyboardDevice::Keys::kLeftControl, RE::BSKeyboardDevice::Keys::kBackspace };

		// Whether the ini has been read. Once per process.
		bool g_read = false;

		// Reads one scan code out of an ini value, decimal or 0x hex.
		//
		// Returns false for anything it does not understand, INCLUDING a value
		// out of range, and the caller keeps the default. A typo that silently
		// became key 0 would disable the escape hatch, which is the one outcome
		// this reader may not produce quietly.
		bool ParseScanCode(std::string_view a_text, std::uint32_t& a_out)
		{
			const std::string trimmed = Config::Trim(a_text);
			if (trimmed.empty()) {
				return false;
			}

			int  base   = 10;
			auto digits = std::string_view(trimmed);
			if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
				base = 16;
				digits.remove_prefix(2);
			}

			unsigned long value = 0;
			try {
				std::size_t consumed = 0;
				value                = std::stoul(std::string(digits), &consumed, base);
				if (consumed != digits.size()) {
					return false;
				}
			} catch (...) {
				return false;
			}

			// The keyboard device's codes are one byte. Anything above that is a
			// typo, not a key.
			if (value == 0 || value > 0xFF) {
				return false;
			}

			a_out = static_cast<std::uint32_t>(value);
			return true;
		}
	}

	// Format is two scan codes separated by '|', matching the shape EquipVeto
	// already uses in the same file.
	Chord Get(const char* a_owner)
	{
		if (g_read) {
			return g_chord;
		}
		g_read = true;

		const std::string_view owner = a_owner ? a_owner : "WebUI";

		std::string raw;
		Config::ForEachPair([&raw](std::string_view a_key, std::string_view a_value) {
			if (a_key == "webuipanickeys") {
				raw = a_value;
			}
		});

		if (raw.empty()) {
			return g_chord;
		}

		const auto separator = raw.find('|');
		if (separator == std::string::npos) {
			spdlog::warn("{}: WebUIPanicKeys is '{}', which is not two scan codes "
						 "separated by '|' - keeping the default Ctrl+Backspace.",
				owner, raw);
			return g_chord;
		}

		std::uint32_t key1 = 0;
		std::uint32_t key2 = 0;
		if (!ParseScanCode(std::string_view(raw).substr(0, separator), key1) ||
			!ParseScanCode(std::string_view(raw).substr(separator + 1), key2)) {
			spdlog::warn("{}: WebUIPanicKeys is '{}', and at least one half is not a "
						 "scan code between 1 and 255 - keeping the default Ctrl+Backspace.",
				owner, raw);
			return g_chord;
		}

		g_chord = Chord{ key1, key2 };
		spdlog::info("{}: panic chord set from Lodestone.ini to scan codes "
					 "0x{:02X} + 0x{:02X}.",
			owner, g_chord.first, g_chord.second);
		return g_chord;
	}
}
