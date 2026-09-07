// WebUIBackend.h
// Lodestone - Shared SKSE framework
//
// The seam between the WebUI bridge and whichever web UI framework is actually
// installed. See WebUIBridge.h for what the bridge is and why it exists.
//
// WHY THIS SEAM EXISTS AT ALL. The 1.18.0 rename argued that "swapping one
// pointer for a vector of adapters later is invisible to every consumer". That
// was a promise, and this file is the first half of testing it: the bridge is
// split here without adding a single behavior, so that if the promise was wrong
// it fails now, with one backend and one consumer, instead of later with four.
//
// WHAT BELONGS ON WHICH SIDE OF IT. The split is not "engine vs logic" - it is
// "does the answer change when the installed framework changes".
//
//   Backend      the vendor's API, its handle type, its callback shapes, its
//                view root, its name, and what it can and cannot do.
//   Coordinator  the view table keyed by the consumer's own string id, the
//                listener slot pool, the mod events, the main-thread dispatch,
//                and all 22 natives. None of that changes with the vendor.
//
// Test, when it is not obvious: if the code would read identically for a
// framework nobody has written yet, it is the coordinator's.
//
// THE OPERATIONS ARE CALLED ON THE MAIN GAME THREAD, ALWAYS. The coordinator
// owns that decision and every one of them arrives through its task queue - see
// DispatchToGame in WebUIBridge.cpp for why that is not optional for a native
// running on the Papyrus VM thread. A backend never has to queue anything of its
// own, and must not assume it may block.
//
// REPORTING BACK RUNS THE OTHER WAY AND ON ANY THREAD. A backend calls the two
// functions in WebUIBackendCallbacks below from whatever thread its framework
// hands it, which is generally not the game thread. Those two are the only
// entry points a backend has into the coordinator.

#pragma once

#include <cstddef>
#include <cstdint>

namespace Lodestone::Core
{
	// Maximum number of JS listener slots, shared by every view and every mod.
	//
	// It lives here rather than in the coordinator because a backend whose
	// callback type carries no context argument has to emit one distinct
	// function per slot, and therefore has to know the count at compile time.
	// Prisma UI's JSListenerCallback is exactly that shape.
	//
	// 32 was chosen as the smallest number that is not going to be met.
	// Consumers register a handful of callbacks per panel. Raising it costs one
	// constant and a recompile.
	inline constexpr std::size_t kWebUIMaxListeners = 32;

	// One installed web UI framework, as the bridge needs to see it.
	//
	// Implementations are stateless singletons owned by their own translation
	// unit and handed out by the accessors at the bottom of this file. They are
	// never created, destroyed or copied: a backend outlives every view it
	// makes, and the process ends before it does.
	class IWebUIBackend
	{
	public:
		// A view, as the backend knows it.
		//
		// WIDE ON PURPOSE, AND NEVER SEEN BY PAPYRUS. Prisma UI's PrismaView is
		// a uint64_t; other frameworks use narrower handles. The widest type
		// carries all of them without a cast at the call site. It is not the
		// key of anything - the consumer names its view with a string it chose,
		// which is what the coordinator's map is keyed by, because the Papyrus
		// Int is 32 bits and would truncate this.
		//
		// Zero means "no view". Every backend here must treat it that way.
		using ViewHandle = std::uint64_t;

		virtual ~IWebUIBackend() = default;

		// --- Identity ---------------------------------------------------------

		// The token WebUIGetBackend() hands to Papyrus. Stable, no spaces: a
		// consumer may print it, and a player may paste it into a bug report.
		virtual const char* Name() const = 0;

		// The vendor's own spelling, for the log line a human reads.
		//
		// Separate from Name() because the two genuinely differ - "PrismaUI" is
		// the wire token, "Prisma UI" is what the mod is called - and collapsing
		// them would change one of the two in a way somebody is already reading.
		virtual const char* DisplayName() const = 0;

		// Where this backend looks for view files, phrased to be dropped into an
		// error message. The bridge cannot say this itself: the path root is the
		// vendor's convention, and it is the single most useful thing to print
		// when a view fails to build.
		virtual const char* ViewRootHint() const = 0;

		// --- Lifecycle --------------------------------------------------------

		// Looks for the framework and takes hold of its API.
		//
		// Called once, from the bridge's Acquire(). Absent is a normal outcome
		// and not an error: it leaves the backend unavailable and says so at
		// info level, never at error level. A probe that logs a failure teaches
		// users to report a non-problem.
		//
		// WHEN THIS RUNS IS THE BACKEND'S BUSINESS. Prisma UI answers a direct
		// request at kPostLoad. Another framework may only answer later, through
		// a message handshake, in which case Probe() arms the handshake and
		// IsAvailable() stays false until it completes. The bridge asks for the
		// answer whenever a native is called, never once at startup, so a
		// backend that arrives late is supported without the bridge knowing it
		// happened.
		virtual void Probe() = 0;

		// Whether this backend is present and usable right now.
		//
		// Read on every native call. Must be cheap and must not block.
		virtual bool IsAvailable() const = 0;

		// --- Capability -------------------------------------------------------

		// Whether this backend supports a named capability.
		//
		// AN UNKNOWN NAME RETURNS false AND LOGS NOTHING. That property is what
		// lets the vocabulary grow without a major version: a consumer built
		// against a newer Lodestone asks an older DLL about a name it never
		// heard of and gets the correct answer.
		//
		// Answered from what the backend can do, never from which backend it is.
		virtual bool HasCapability(const char* a_capability) const = 0;

		// --- View operations --------------------------------------------------
		//
		// All six run on the main game thread. All six are given a handle this
		// backend itself produced, except CreateView which produces one.

		// Builds a view from a path relative to this backend's view root.
		//
		// Returns 0 on failure. The backend is responsible for arranging that
		// WebUIBackendCallbacks::ViewReady fires for the returned handle once
		// the page has finished loading - by registering the framework's own
		// callback, or by whatever else that framework offers.
		virtual ViewHandle CreateView(const char* a_viewPath) = 0;

		// Tears the view down and releases the handle.
		virtual void DestroyView(ViewHandle a_view) = 0;

		virtual void Show(ViewHandle a_view) = 0;
		virtual void Hide(ViewHandle a_view) = 0;

		// Calls a_function on the page's JS interop surface with a_json as its
		// single argument.
		virtual void Call(ViewHandle a_view, const char* a_function, const char* a_json) = 0;

		// Makes the page's JS call to a_jsFunction reach
		// WebUIBackendCallbacks::ListenerFired with a_slot.
		//
		// The slot index is chosen by the coordinator, which owns the pool. The
		// backend's only job is to get that number back out again when the page
		// fires - through a per-slot function, a context pointer, or whatever
		// its framework provides.
		//
		// a_slot is always below kWebUIMaxListeners.
		virtual void RegisterListener(ViewHandle a_view, const char* a_jsFunction, std::size_t a_slot) = 0;
	};

	// The only way back in. Called by a backend, from any thread.
	namespace WebUIBackendCallbacks
	{
		// A view's page has finished loading.
		//
		// The handle is enough to find the view: handles are unique within a
		// backend, and only one backend is active per session. An unknown handle
		// is ignored - it means the view was destroyed between the framework's
		// call and this one.
		void ViewReady(IWebUIBackend* a_backend, IWebUIBackend::ViewHandle a_view);

		// A page fired the JS function registered against a_slot.
		//
		// a_argument may be null. It does not outlive the call, so the
		// coordinator copies it before doing anything asynchronous.
		void ListenerFired(std::size_t a_slot, const char* a_argument);
	}

	// The backends this DLL was built with.
	//
	// One accessor per backend rather than a table, because the set is fixed at
	// compile time and naming them here is what makes a missing one a link
	// error instead of an empty list at runtime. The bridge holds the order.
	namespace WebUIBackends
	{
		IWebUIBackend* PrismaUI();
	}
}
