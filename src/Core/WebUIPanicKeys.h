// WebUIPanicKeys.h
// Lodestone - Shared SKSE framework
//
// The reader for WebUIPanicKeys in Lodestone.ini - the chord that takes the mouse
// and keyboard back from a focused WebUI view.
//
// SHARED, AND ONLY THE READER IS. Both backends honour the same setting, with
// the same parser and the same default, so a player sets it once and it works
// whichever backend the session picked. What each backend DOES with the chord is
// its own and differs on purpose: Meridian registers it inside the platform as a
// toggle, Prisma watches for it with an input sink of Lodestone's and only ever
// releases. See the two backend files.
//
// It lived inside MeridianUIBackend.cpp until 1.27.0, when the second backend
// needed it. The log lines are unchanged: the owner name each caller passes is
// the prefix those lines always had.

#pragma once

#include <cstdint>

namespace Lodestone::Core::WebUIPanicKeys
{
	// Two RE::BSKeyboardDevice::Keys scan codes. Order does not matter: the chord
	// is complete when both are down, whichever went down first.
	struct Chord
	{
		std::uint32_t first;
		std::uint32_t second;
	};

	// The chord, read from Lodestone.ini on the first call and remembered after.
	//
	// GAME THREAD ONLY. Both callers are there already, and a lock would guard a
	// read that happens once per process.
	//
	// a_owner prefixes the lines this writes - "MeridianUIBackend" produces the
	// same lines that backend wrote before the reader moved. Only one backend is
	// active per session, so only one owner ever asks, and the lines are written
	// once, on the call that actually reads.
	//
	// An absent value is the default Ctrl+Backspace and says nothing. An
	// unreadable one is the default too, and says so at warning level, because a
	// player who tried to set this and got the default needs to know.
	Chord Get(const char* a_owner);
}
