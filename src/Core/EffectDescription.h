// EffectDescription.h
// Lodestone - Shared SKSE framework
//
// Module: EffectDescription
// Reads a MagicEffect's description text (EffectSetting::magicItemDescription,
// the DNAM field) for Papyrus. Read-only and stateless: no hook, no cosave,
// nothing cached - it is a separate module from MagicScaling on purpose, because
// that one owns engine hooks and registered channels and this owns neither.
//
// Papyrus-facing script: Lodestone.psc
//
// Phase L-F2

#pragma once

namespace Lodestone::Core::EffectDescription
{
	// Registers this module's native functions with the Papyrus VM.
	// Called by Lodestone::Core::Papyrus::Register - never called directly.
	//
	// Returns false if any registration failed.
	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm);
}
