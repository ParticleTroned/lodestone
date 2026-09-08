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

			// Nothing. Prisma is settled by Probe() and needs no seam of its
			// own - see the interface, where this is the expected shape for a
			// backend acquired by direct request rather than by handshake.
			void HandleSKSEMessage(SKSE::MessagingInterface::Message*) override {}

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
				//
				// IT STAYS false, AND 1.22.0 IS EXACTLY WHEN SOMEBODY WILL TRY
				// TO "FIX" IT. That version added "view-focus" below, so this
				// line now sits next to a focus capability that answers
				// differently, and the two look like they disagree. They do not.
				// They are different questions - see the trap written out under
				// "view-focus" - and this one has the same answer on both
				// backends for two different reasons.
				if (capability == "focus-stack") {
					return false;
				}

				// "view-focus": can ONE view be given the mouse and keyboard.
				//
				// THIS IS NOT "focus-stack" WITH A SHORTER NAME, and reading it
				// that way is the mistake this comment exists to stop:
				//
				//   focus-stack   can TWO views hold focus independently?
				//   view-focus    can ONE view receive a click at all?
				//
				// A backend can answer no to the first and yes to the second,
				// and the other one does. A consumer that asks "focus-stack"
				// meaning "can my panel take a click" gets a wrong answer on
				// every backend, in both directions over time: false before
				// 1.22.0 because the surface did not exist, and false after it
				// because that is genuinely the answer to a question it did not
				// mean to ask.
				//
				// False here. The reason is long and belongs with the code it
				// governs - see SetFocus below. In one line: the framework
				// captures input per process rather than per view, its unfocus
				// strands a second view's cursor, and it publishes no panic key
				// to escape with.
				if (capability == "view-focus") {
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

			// The view id is unused here: Prisma names nothing and hands back an
			// opaque handle. It is in the signature for Meridian's sake.
			ViewHandle CreateView(const char*, const char* a_viewPath) override
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

			// --- Focus, which this backend declines ------------------------
			//
			// UNREACHABLE, NOT UNIMPLEMENTED. HasCapability answers false to
			// "view-focus", and the coordinator asks that before it dispatches,
			// so neither of these is ever called. They log if they are, because
			// a call arriving here means the coordinator's gate broke and a
			// silent no-op would hide that.
			//
			// WHY THE CAPABILITY IS false, AND IT IS NOT THAT THE API IS
			// MISSING. PrismaUI_API.h has Focus, Unfocus, HasFocus and
			// HasAnyActiveFocus, all of them per view, and calling them would
			// compile and would do something. Three measured facts say not to:
			//
			//   1. Focus is not a local operation here. The framework routes
			//      input for the whole PROCESS, not per view: one elected view
			//      id, one capture flag, one "a text field has focus" flag.
			//      Focusing one view takes the keyboard from every other Prisma
			//      consumer in the game, including ones that never heard of
			//      Lodestone.
			//   2. Unfocus closes the framework's single kModal focus menu for
			//      every view at once, so a second view on screen is left with a
			//      stranded cursor. A sibling project of this tree exhausted the
			//      four-way flag matrix in game - both pauseGame and
			//      disableFocusMenu, all combinations - and found no mitigation.
			//      The two public flags do not touch the broken path.
			//   3. There is no panic key to escape with. The other backend
			//      publishes an unswallowable chord of its own
			//      (ToggleBrowserFocusByKeys, IBrowser.h); this API has no
			//      equivalent, and Lodestone installs no input sink anywhere, so
			//      a stranded cursor here would leave killing the process as the
			//      only way out.
			//
			// Answering false costs a consumer an interactive panel on this
			// backend and costs it nothing else: it asks, it gets an honest no,
			// and it degrades. Answering true would trade that for a failure
			// mode the player pays for and cannot escape.
			//
			// THIS IS A DECISION, NOT A CEILING. It reverses the day somebody
			// measures the deferred-unfocus path in game and can say what
			// HasAnyActiveFocus() actually reports while an unfocus is still
			// queued. Growing false into true is invisible to every consumer
			// that already asks first, which is why the capability shipped
			// before the feature.
			bool SetFocus(ViewHandle) override
			{
				spdlog::error("PrismaUIBackend: SetFocus reached a backend that answers false to "
							  "\"view-focus\" - the coordinator should not have dispatched this.");
				return false;
			}

			void ClearFocus(ViewHandle) override
			{
				spdlog::error("PrismaUIBackend: ClearFocus reached a backend that answers false to "
							  "\"view-focus\" - the coordinator should not have dispatched this.");
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
