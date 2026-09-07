// Config.h
// Lodestone - Shared SKSE framework
//
// The one reader for Data\SKSE\Plugins\Lodestone.ini.
//
// WHY THIS EXISTS NOW AND NOT BEFORE. The ini's own header said it: "the day a
// second module needs configuration the parser has to learn to scope keys". The
// second module arrived when the WebUI bridge gained a backend to choose
// between, and the parser that had been living inside Core/EquipVeto came out
// here rather than being copied.
//
// STILL FLAT, AND THAT IS A DECISION. Section headers are skipped, exactly as
// they always were, so `[EquipVeto]` and `[WebUI]` are organisational and a key
// works wherever it sits. Scoping keys by section is the larger half of what
// that note predicted, and it would rewrite a configuration path that ships and
// works today, for a collision that does not exist: every key in this file has
// a distinct name, and new ones are expected to keep doing that. When two
// modules genuinely want the same key name, this is where that gets fixed.
//
// The practical consequence for whoever adds a key: NAME IT SO IT CANNOT
// COLLIDE. `WebUIBackend`, not `backend`.

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace Lodestone::Core::Config
{
	// Where the file lives, relative to the game directory.
	inline constexpr std::string_view kPath = "Data/SKSE/Plugins/Lodestone.ini";

	// Strips spaces, tabs and both line-ending characters from each end.
	//
	// Shared because a value can need trimming again after the reader has
	// handed it over - EquipVeto splits one on '|' and trims each half.
	std::string Trim(std::string_view a_text);

	// ASCII case-insensitive comparison.
	//
	// Shared for the same reason Trim is: a value read from this file gets
	// compared against a name the code knows, and the user typed it. EquipVeto
	// matches plugin filenames with it; the bridge matches a backend name.
	bool EqualsNoCase(std::string_view a_lhs, std::string_view a_rhs);

	// Calls a_onPair once for every `key = value` line, in file order.
	//
	// The key arrives LOWERCASED and both sides trimmed. Blank lines, `;` and
	// `#` comments and `[section]` headers are skipped; a line with no `=` is
	// skipped.
	//
	// EVERY OCCURRENCE IS DELIVERED, NOT THE LAST ONE. A map would have been
	// the obvious shape and would have been wrong: EquipVeto's WatchActor and
	// WatchFaction are meant to repeat, and each line adds to a list.
	//
	// Returns false when the file could not be opened, which is a normal state
	// - the file is optional - and is left to the caller to report, because
	// what its absence means differs per module.
	bool ForEachPair(const std::function<void(std::string_view a_key, std::string_view a_value)>& a_onPair);
}
