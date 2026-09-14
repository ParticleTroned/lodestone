// SpellRead.cpp
// Lodestone - Shared SKSE framework
//
// Natives behind Lodestone.GetSpellSchools, GetSpellSkillLevels and
// GetSpellNames: each takes a Spell[] and hands back an array of the same
// length, one answer per position.
//
// WHY BATCHES. A Papyrus native registered without "callable from tasklets"
// waits for the game's main thread - about one frame per call. Measured by the
// Intelligence Matters consumer (probe Q, 123 spells, 2026-09-13):
// MagicEffect.GetAssociatedSkill cost 17.1 ms per call and Form.GetName 23.2 ms
// per call. One call here reads the whole array for one frame. These stay on the
// default registration like every other native in this plugin - three calls,
// three frames - so nothing here runs off the main thread.
//
// WHICH EFFECT. School and level both come from the COSTLIEST effect, the same
// effect for both, through the engine's own MagicItem::GetCostliestEffectItem
// with its default arguments. The magic menu takes school and level from the
// costliest effect (measured in game by the consumer, 2026-09-10). That the
// default arguments pick the same effect as the menu and as SKSE's
// Spell.GetCostliestEffectIndex is NOT measured here: the consumer's probe
// compares element by element.
//
// SCHOOL TEXT. ActorValueList::GetActorValueName - the engine's own name for the
// actor value - with a null name returned as "". Deliberately not
// ActorValueToString, which turns a null name into "None". The goal is the exact
// text MagicEffect.GetAssociatedSkill returns in Papyrus; that the two agree,
// above all for an effect with no school, is INFERRED and checked by the same
// probe. Values outside the actor value table (kNone included) never reach the
// engine lookup and come back as "".
//
// Phase L-F3

#include "SpellRead.h"

namespace Lodestone::Core::SpellRead
{
	namespace
	{
		// The base effect school and level are read from: the spell's costliest
		// effect. nullptr when the spell is None, has no effects, or that effect
		// has no base effect.
		const RE::EffectSetting* CostliestBaseEffect(const RE::SpellItem* a_spell)
		{
			if (!a_spell || a_spell->effects.empty()) {
				return nullptr;
			}

			const RE::Effect* effect = a_spell->GetCostliestEffectItem();
			return effect ? effect->baseEffect : nullptr;
		}

		std::string SchoolName(const RE::EffectSetting* a_base)
		{
			if (!a_base) {
				return {};
			}

			const RE::ActorValue skill = a_base->GetMagickSkill();
			const auto            index = static_cast<std::int32_t>(skill);
			if (index < 0 || index >= static_cast<std::int32_t>(RE::ActorValue::kTotal)) {
				return {};
			}

			const char* name = RE::ActorValueList::GetActorValueName(skill);
			return name ? std::string(name) : std::string();
		}

		// Lodestone.GetSpellSchools(Spell[]) -> String[]
		//
		// "" for a None position, a spell with no effects, or an effect with no
		// associated skill. The result always has the input's length.
		std::vector<std::string> GetSpellSchools(RE::StaticFunctionTag*, std::vector<RE::SpellItem*> a_spells)
		{
			try {
				std::vector<std::string> out(a_spells.size());
				for (std::size_t i = 0; i < a_spells.size(); ++i) {
					out[i] = SchoolName(CostliestBaseEffect(a_spells[i]));
				}
				return out;
			} catch (...) {
				return std::vector<std::string>(a_spells.size());
			}
		}

		// Lodestone.GetSpellSkillLevels(Spell[]) -> Int[]
		//
		// -1 for a None position or a spell with no effects. The result always has
		// the input's length.
		std::vector<std::int32_t> GetSpellSkillLevels(RE::StaticFunctionTag*, std::vector<RE::SpellItem*> a_spells)
		{
			try {
				std::vector<std::int32_t> out(a_spells.size(), -1);
				for (std::size_t i = 0; i < a_spells.size(); ++i) {
					const RE::EffectSetting* base = CostliestBaseEffect(a_spells[i]);
					if (base) {
						out[i] = base->GetMinimumSkillLevel();
					}
				}
				return out;
			} catch (...) {
				return std::vector<std::int32_t>(a_spells.size(), -1);
			}
		}

		// Lodestone.GetSpellNames(Spell[]) -> String[]
		//
		// "" for a None position or a spell with no name. A spell with no effects
		// still has its name returned. The result always has the input's length.
		std::vector<std::string> GetSpellNames(RE::StaticFunctionTag*, std::vector<RE::SpellItem*> a_spells)
		{
			try {
				std::vector<std::string> out(a_spells.size());
				for (std::size_t i = 0; i < a_spells.size(); ++i) {
					if (a_spells[i]) {
						out[i] = a_spells[i]->GetName();
					}
				}
				return out;
			} catch (...) {
				return std::vector<std::string>(a_spells.size());
			}
		}
	}

	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm)
	{
		if (!a_vm) {
			spdlog::error("SpellRead: null VM, cannot register natives.");
			return false;
		}

		a_vm->RegisterFunction("GetSpellSchools", "Lodestone", GetSpellSchools);
		a_vm->RegisterFunction("GetSpellSkillLevels", "Lodestone", GetSpellSkillLevels);
		a_vm->RegisterFunction("GetSpellNames", "Lodestone", GetSpellNames);

		spdlog::info("SpellRead: natives registered (GetSpellSchools, GetSpellSkillLevels, GetSpellNames).");
		return true;
	}
}
