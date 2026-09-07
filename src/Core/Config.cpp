// Config.cpp
// Lodestone - Shared SKSE framework
//
// See Config.h for why this is one reader and why it is still flat.
//
// The body is Core/EquipVeto's parser, moved verbatim in behavior: same
// characters trimmed, same lines skipped, same lowercasing of the key only.
// That module ships and works, and this extraction is not the place to improve
// how it reads its own configuration.

#include "Config.h"

#include <cctype>
#include <fstream>

namespace Lodestone::Core::Config
{
	namespace
	{
		std::string ToLower(std::string a_text)
		{
			for (auto& c : a_text) {
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return a_text;
		}
	}

	std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string{ a_text.substr(first, last - first + 1) };
	}

	bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs)
	{
		if (a_lhs.size() != a_rhs.size()) {
			return false;
		}

		for (std::size_t i = 0; i < a_lhs.size(); ++i) {
			if (std::tolower(static_cast<unsigned char>(a_lhs[i])) !=
				std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
				return false;
			}
		}

		return true;
	}

	bool ForEachPair(const std::function<void(std::string_view, std::string_view)>& a_onPair)
	{
		if (!a_onPair) {
			return false;
		}

		std::ifstream file{ std::string{ kPath } };
		if (!file) {
			return false;
		}

		std::string line;
		while (std::getline(file, line)) {
			const auto trimmed = Trim(line);
			if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' ||
				trimmed.front() == '[') {
				continue;
			}

			const auto eq = trimmed.find('=');
			if (eq == std::string::npos) {
				continue;
			}

			const auto key   = ToLower(Trim(std::string_view{ trimmed }.substr(0, eq)));
			const auto value = Trim(std::string_view{ trimmed }.substr(eq + 1));

			a_onPair(key, value);
		}

		return true;
	}
}
