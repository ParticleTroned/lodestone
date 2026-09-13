// EffectDescription.cpp
// Lodestone - Shared SKSE framework
//
// Native implementation behind Lodestone.GetEffectDescription.
//
// Hands back EffectSetting::magicItemDescription as the engine keeps it, with
// markers such as <mag> and <dur> still in the text. Nothing is substituted
// here: the caller holds the numbers, and the magic menu's own display
// formatting is not something this plugin reimplements.
//
// MEASURED BEFORE THIS WAS WRITTEN (phase L-F2, a throwaway probe on DLL
// 1.23.2, development load order): the field holds RESOLVED text for localized
// plugins too. Skyrim.esm is localized, and its Firebolt effect (0x012F03) came
// back as the whole sentence, not as a string-table id; the per-master counts
// of described effects matched a read of the plugin files. That is the only
// reason this can be a plain read instead of a string-table lookup.
//
// ONE DEPARTURE FROM "AS STORED": a description made only of whitespace comes
// back as "". Vanilla ships two ("\r\n" on 0x10192D, " " on 0x082A36) and the
// measured load order nine. On a card they mean "no description", and a caller
// testing for "" would otherwise draw an empty line. Any other text is returned
// untouched, leading and trailing whitespace included.
//
// Phase L-F2

#include "EffectDescription.h"

namespace Lodestone::Core::EffectDescription
{
	namespace
	{
		// Lodestone.GetEffectDescription(MagicEffect) -> String
		//
		// The effect's description text, raw, or "" when the effect is None, has
		// no description, or has one made only of whitespace. "" is an ordinary
		// answer here, not a failure - close to half of all vanilla effects have
		// no text.
		//
		// A plain read of form data this plugin never writes: no hook, no state,
		// nothing cached. Wrapped anyway, because no native in this plugin lets
		// an exception reach the VM (PluginInfo.cpp, API CONVENTION).
		RE::BSFixedString GetEffectDescription(RE::StaticFunctionTag*, RE::EffectSetting* a_effect)
		{
			try {
				if (!a_effect) {
					return RE::BSFixedString("");
				}

				const std::string_view text = a_effect->magicItemDescription.c_str();
				if (text.find_first_not_of(" \t\r\n") == std::string_view::npos) {
					return RE::BSFixedString("");
				}

				return a_effect->magicItemDescription;
			} catch (...) {
				return RE::BSFixedString("");
			}
		}
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			spdlog::error("EffectDescription: null VM, cannot register natives.");
			return false;
		}

		a_vm->RegisterFunction("GetEffectDescription", "Lodestone", GetEffectDescription);

		spdlog::info("EffectDescription: natives registered (GetEffectDescription).");
		return true;
	}
}
