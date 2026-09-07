// PrismaUIBackend.cpp
// Lodestone - Shared SKSE framework
//
// The Prisma UI half of the WebUI bridge. Everything vendor-specific about
// Prisma lives here and nowhere else; see WebUIBackend.h for what the seam is
// and WebUIBridge.h for what the bridge is.
//
// THIS FILE IS THE ONLY ONE THAT INCLUDES PrismaUI_API.h, and that is worth more
// than tidiness. That header is third-party, under a proprietary
// source-available license, and is deliberately NOT tracked in this repository -
// whoever builds copies it into extern/prismaui-api/ by hand. Confining it to
// one translation unit means the rest of the bridge compiles and reads without
// it, and a future decision to drop this backend is a file deletion.
//
// NOTHING HERE KNOWS ABOUT VIEW IDS, LISTENER OWNERSHIP OR MOD EVENTS. Those are
// the coordinator's, keyed by the string the consumer chose. This file deals in
// PrismaView handles and slot indices and nothing else.

#include "WebUIBackend.h"

#include "PrismaUI_API.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace Lodestone::Core
{
	namespace
	{
		// Written once by Probe() on kPostLoad, read from the Papyrus VM thread,
		// the Ultralight thread and the main game thread afterwards. Null means
		// Prisma UI is not installed, which is a supported state and not an
		// error.
		PRISMA_UI_API::IVPrismaUI1* g_api = nullptr;

		// Prisma's DOM-ready callback, which it invokes on the Ultralight thread.
		//
		// THIS CALLBACK CARRIES THE VIEW, which is what makes one shared function
		// enough here and is exactly why the JS listeners below need a table and
		// this does not.
		//
		// Wrapped because it returns into Prisma: an exception leaving it unwinds
		// into third-party code, which is undefined behavior for the same reason
		// letting one cross into the Papyrus VM is.
		void OnDomReady(PrismaView a_view)
		{
			try {
				WebUIBackendCallbacks::ViewReady(WebUIBackends::PrismaUI(), a_view);
			} catch (...) {
				spdlog::error("PrismaUIBackend: OnDomReady threw - the view is usable but no ready "
							  "event was sent.");
			}
		}

		// --- JS listener thunks -------------------------------------------------
		//
		// PrismaUI's JSListenerCallback is void(*)(const char*). It carries NO
		// context argument, so there is no way to hand the framework a lambda
		// that knows which slot it belongs to - a capturing lambda does not
		// convert to a plain function pointer at all.
		//
		// The way out is a fixed table of distinct functions, each of which knows
		// its own index at compile time. That is the reason kWebUIMaxListeners
		// has to be a compile-time constant: the number of listeners is the
		// number of functions the compiler was asked to emit.
		//
		// Wrapped for the same reason OnDomReady is - these return into Prisma.
		template <std::size_t N>
		void ListenerThunk(const char* a_argument)
		{
			try {
				WebUIBackendCallbacks::ListenerFired(N, a_argument);
			} catch (...) {
				spdlog::error("PrismaUIBackend: a JS listener threw on slot {} - the mod event was "
							  "not sent.",
					N);
			}
		}

		template <std::size_t... I>
		constexpr std::array<PRISMA_UI_API::JSListenerCallback, sizeof...(I)> MakeThunkTable(std::index_sequence<I...>)
		{
			return { &ListenerThunk<I>... };
		}

		const auto g_thunks = MakeThunkTable(std::make_index_sequence<kWebUIMaxListeners>{});

		// --- The backend --------------------------------------------------------

		class PrismaUIBackend final : public IWebUIBackend
		{
		public:
			const char* Name() const override { return "PrismaUI"; }
			const char* DisplayName() const override { return "Prisma UI"; }

			// Phrased to be dropped straight into the coordinator's CreateView
			// error, which is where a consumer with a typo in a path first
			// looks.
			const char* ViewRootHint() const override { return "Data\\PrismaUI\\views for Prisma UI"; }

			// Prisma answers a direct request, so presence is settled here and
			// IsAvailable() never changes afterwards.
			//
			// NO VIEW IS CREATED HERE, and that is the trap this module was
			// written around. At kPostLoad the D3D device and the Ultralight
			// renderer do not exist yet, and CreateView at that moment queues
			// forever or blocks the load chain - established from the Add Item
			// Menu's own source and paid for again by a sibling project of this
			// tree.
			void Probe() override
			{
				g_api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();
			}

			bool IsAvailable() const override { return g_api != nullptr; }

			bool HasCapability(const char* a_capability) const override
			{
				if (!a_capability) {
					return false;
				}

				const std::string_view capability(a_capability);

				// "focus-stack": can two views hold focus independently.
				//
				// False, and this is measured, not assumed. Prisma exposes Focus
				// and Unfocus per view, but its focus menu is a single kModal
				// with no stack: unfocusing one closes it for all of them. The
				// capability exists; the stacking does not. See WebUIBridge.h.
				if (capability == "focus-stack") {
					return false;
				}

				// "view-order": can a view's stacking order be set.
				// PrismaUI_API.h SetOrder / GetOrder.
				if (capability == "view-order") {
					return true;
				}

				// "inspector": can a developer inspector be opened on a view.
				// PrismaUI_API.h CreateInspectorView.
				if (capability == "inspector") {
					return true;
				}

				return false;
			}

			ViewHandle CreateView(const char* a_viewPath) override
			{
				return g_api ? static_cast<ViewHandle>(g_api->CreateView(a_viewPath, &OnDomReady)) : 0;
			}

			void DestroyView(ViewHandle a_view) override
			{
				if (g_api) {
					g_api->Destroy(static_cast<PrismaView>(a_view));
				}
			}

			void Show(ViewHandle a_view) override
			{
				if (g_api) {
					g_api->Show(static_cast<PrismaView>(a_view));
				}
			}

			void Hide(ViewHandle a_view) override
			{
				if (g_api) {
					g_api->Hide(static_cast<PrismaView>(a_view));
				}
			}

			void Call(ViewHandle a_view, const char* a_function, const char* a_json) override
			{
				if (g_api) {
					g_api->InteropCall(static_cast<PrismaView>(a_view), a_function, a_json);
				}
			}

			void RegisterListener(ViewHandle a_view, const char* a_jsFunction, std::size_t a_slot) override
			{
				if (g_api && a_slot < g_thunks.size()) {
					g_api->RegisterJSListener(static_cast<PrismaView>(a_view), a_jsFunction, g_thunks[a_slot]);
				}
			}
		};

		PrismaUIBackend g_backend;
	}

	namespace WebUIBackends
	{
		IWebUIBackend* PrismaUI()
		{
			return &g_backend;
		}
	}
}
