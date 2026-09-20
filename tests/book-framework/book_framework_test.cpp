#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace RE
{
	using FormID = std::uint32_t;
	struct BSString
	{
		inline static bool failReplacement = false;
		std::string text;
		BSString() = default;
		explicit BSString(std::string_view value) : text(value)
		{
			if (failReplacement) {
				throw std::runtime_error("replacement allocation failed");
			}
		}
	};
	struct BSFixedString
	{
		std::string text;
		explicit BSFixedString(const char* value) : text(value) {}
		const char* c_str() const { return text.c_str(); }
	};
	struct ExtraDataList
	{};
	struct TESObjectREFR
	{};
	struct TESObjectBOOK
	{
		FormID GetFormID() const { return 0x42; }
		const char* GetName() const { return "Test book"; }
	};
	struct NiPoint3
	{};
	struct NiMatrix3
	{};
	struct NiAVObject
	{};
	struct StaticFunctionTag
	{};
	namespace BSScript
	{
		struct IVirtualMachine
		{
			template <class Fn>
			void RegisterFunction(const char*, const char*, Fn)
			{}
		};
	}
}

namespace REL
{
	inline bool vr = false;
	inline std::uintptr_t target = 0;
	struct Module
	{
		static bool IsVR() { return vr; }
	};
	struct RelocationID
	{
		RelocationID(int, int) {}
	};
	template <class T>
	struct Relocation
	{
		explicit Relocation(RelocationID) {}
		T address() const { return static_cast<T>(target); }
	};
}

namespace spdlog
{
	namespace level
	{
		enum level_enum
		{
			debug
		};
	}
	struct logger
	{
		bool should_log(level::level_enum) const { return false; }
	};
	inline logger* default_logger_raw() { return nullptr; }
	template <class... Args>
	void debug(Args...)
	{}
	template <class... Args>
	void info(Args...)
	{}
	template <class... Args>
	void warn(Args...)
	{}
	template <class... Args>
	void error(Args...)
	{}
}

// Compile the production implementation; only its game and detour services
// are replaced so both native signatures can run in a standalone process.
#include "../../src/Core/BookFramework.cpp"

namespace
{
	using FlatOpen = void (*)(const RE::BSString&, const RE::ExtraDataList*, RE::TESObjectREFR*,
		RE::TESObjectBOOK*, const RE::NiPoint3&, const RE::NiMatrix3&, float, bool);
	using VrOpen = void (*)(const RE::BSString&, const RE::ExtraDataList*, RE::TESObjectREFR*,
		RE::TESObjectBOOK*, const RE::NiPoint3&, const RE::NiMatrix3&, float, bool, RE::NiAVObject*);
	using namespace Lodestone::Core::BookFramework;
	static_assert(std::is_same_v<decltype(&OpenBookMenuHook::thunk<>), FlatOpen>);
	static_assert(std::is_same_v<decltype(&OpenBookMenuHook::thunk<RE::NiAVObject*>), VrOpen>);

	RE::ExtraDataList extra;
	RE::TESObjectREFR reference;
	RE::TESObjectBOOK book;
	RE::NiPoint3 position;
	RE::NiMatrix3 rotation;
	RE::NiAVObject scene;
	const RE::BSString description{ "original" };
	RE::TESObjectBOOK* expectedBook = nullptr;
	RE::NiAVObject* expectedScene = nullptr;
	std::string expectedText;
	bool expectedDefaultPosition = false;
	bool expectOriginalDescription = true;
	int calls = 0;
	int cases = 0;

	void Require(bool condition, const char* message)
	{
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	void CheckCommon(const RE::BSString& text, const RE::ExtraDataList* extras,
		RE::TESObjectREFR* ref, RE::TESObjectBOOK* form, const RE::NiPoint3& pos,
		const RE::NiMatrix3& rot, float scale, bool defaultPosition)
	{
		++calls;
		Require(text.text == expectedText, "book text changed unexpectedly");
		Require((&text == &description) == expectOriginalDescription, "description identity changed");
		Require(extras == &extra && ref == &reference && form == expectedBook, "form arguments changed");
		Require(&pos == &position && &rot == &rotation, "transform references changed");
		Require(scale == 0.4375f && defaultPosition == expectedDefaultPosition, "scale or bool changed");
	}

#if defined(_MSC_VER)
#	define BOOK_TEST_NOINLINE __declspec(noinline)
#else
#	define BOOK_TEST_NOINLINE __attribute__((noinline))
#endif
	BOOK_TEST_NOINLINE void FlatOriginal(const RE::BSString& text, const RE::ExtraDataList* extras,
		RE::TESObjectREFR* ref, RE::TESObjectBOOK* form, const RE::NiPoint3& pos,
		const RE::NiMatrix3& rot, float scale, bool defaultPosition)
	{
		CheckCommon(text, extras, ref, form, pos, rot, scale, defaultPosition);
	}

	BOOK_TEST_NOINLINE void VrOriginal(const RE::BSString& text, const RE::ExtraDataList* extras,
		RE::TESObjectREFR* ref, RE::TESObjectBOOK* form, const RE::NiPoint3& pos,
		const RE::NiMatrix3& rot, float scale, bool defaultPosition, RE::NiAVObject* source)
	{
		CheckCommon(text, extras, ref, form, pos, rot, scale, defaultPosition);
		Require(source == expectedScene, "ninth VR argument was lost");
	}

	void Run(bool vr, RE::NiAVObject* source, bool defaultPosition, int mode)
	{
		REL::vr = vr;
		REL::target = vr ? reinterpret_cast<std::uintptr_t>(&VrOriginal) : reinterpret_cast<std::uintptr_t>(&FlatOriginal);
		Install();
		Require(safetyhook::installedThunk == (vr ? reinterpret_cast<void*>(&OpenBookMenuHook::thunk<RE::NiAVObject*>) : reinterpret_cast<void*>(&OpenBookMenuHook::thunk<>)), "wrong installed signature");
		g_bookText.clear();
		if (mode != 0) {
			g_bookText[book.GetFormID()] = mode == 2 ? "" : "replacement";
		}
		RE::BSString::failReplacement = mode == 3;
		expectedBook = mode == 4 ? nullptr : &book;
		expectedScene = source;
		expectedDefaultPosition = defaultPosition;
		expectOriginalDescription = mode == 0 || mode >= 3;
		expectedText = expectOriginalDescription ? "original" : (mode == 2 ? "" : "replacement");
		calls = 0;
		if (vr) {
			reinterpret_cast<VrOpen>(safetyhook::installedThunk)(description, &extra, &reference, expectedBook, position, rotation, 0.4375f, defaultPosition, source);
		} else {
			reinterpret_cast<FlatOpen>(safetyhook::installedThunk)(description, &extra, &reference, expectedBook, position, rotation, 0.4375f, defaultPosition);
		}
		Require(calls == 1, "original was not called exactly once");
		++cases;
	}
}

int main()
{
	try {
		for (bool defaultPosition : { false, true }) {
			for (int mode = 0; mode != 5; ++mode) {
				Run(false, nullptr, defaultPosition, mode);
				Run(true, nullptr, defaultPosition, mode);
				Run(true, &scene, defaultPosition, mode);
			}
		}
		std::cout << cases << " book forwarding cases passed\n";
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
