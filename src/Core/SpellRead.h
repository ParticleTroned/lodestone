// SpellRead.h
// Lodestone - Shared SKSE framework
//
// Module: SpellRead
// Batch readers over a Spell[] for Papyrus: school, skill level and name of each
// spell, handed back as parallel arrays. Read-only and stateless: no hook, no
// cosave, nothing cached. A separate module from EffectDescription because that
// one reads a MagicEffect and this reads SpellItem, and from MagicScaling
// because that one owns engine hooks and registered channels.
//
// Papyrus-facing script: Lodestone.psc
//
// Phase L-F3

#pragma once

namespace Lodestone::Core::SpellRead
{
	// Registers this module's native functions with the Papyrus VM.
	// Called by Lodestone::Core::Papyrus::Register - never called directly.
	//
	// Returns false if any registration failed.
	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm);
}
