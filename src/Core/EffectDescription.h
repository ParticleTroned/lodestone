// EffectDescription.h
// Lodestone - Shared SKSE framework
//
// Module: EffectDescription
// Reads and writes a MagicEffect's description text
// (EffectSetting::magicItemDescription, the DNAM field) for Papyrus. No hook,
// no cosave: a write goes straight to the field, queued onto the game thread,
// and the original text for a written effect is kept in memory for the
// session so Clear can restore it. It is a separate module from MagicScaling
// on purpose, because that one owns engine hooks and registered channels and
// this owns neither.
//
// Papyrus-facing script: Lodestone.psc
//
// Phase L-F2 (read), L-F4 (write)

#pragma once

namespace Lodestone::Core::EffectDescription
{
	// Registers this module's native functions with the Papyrus VM.
	// Called by Lodestone::Core::Papyrus::Register - never called directly.
	//
	// Returns false if any registration failed.
	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm);
}
