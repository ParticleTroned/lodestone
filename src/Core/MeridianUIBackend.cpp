// MeridianUIBackend.cpp
// Lodestone - Shared SKSE framework
//
// The Meridian UI half of the WebUI bridge. See WebUIBackend.h for the seam and
// WebUIBridge.h for what the bridge is.
//
// Meridian UI is an independently maintained fork of NirnLabUIPlatform, backed
// by CEF rather than Ultralight. Its API headers are vendored in
// extern/meridianui-api/ under their own MIT license - unlike Prisma's, which
// cannot be tracked here. See the README in that folder.
//
// NEITHER BACKEND IS A DEPENDENCY. Meridian is detected at runtime and its
// absence is the ordinary case, exactly like Prisma's.
//
// THREE THINGS DIFFER FROM PRISMA, AND THEY ARE WHAT THIS FILE IS FOR:
//
//   1. Acquisition is a two-step SKSE handshake, not a request. The pointer
//      does not exist until kInputLoaded. See Probe() and HandleSKSEMessage().
//   2. There is NO page-ready callback anywhere in the API - only the
//      IsPageLoaded() poll. So this file watches, on the game thread, and
//      reports readiness itself. See the watcher below.
//   3. JS bindings live under an object, not on the bare window, and calls into
//      the page go through ExecuteJavaScript rather than a dedicated entry
//      point. Both are papered over so a page written for one backend works on
//      the other. See kJsObjectName and Call().

#include "WebUIBackend.h"

#include "API.h"
#include "SKSELoader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace Lodestone::Core
{
	namespace
	{
		// The mod:// host every view of this bridge is served from, and the
		// folder a consumer installs its page into.
		//
		// ONE HOST FOR THE WHOLE BRIDGE, not one per consumer, because a Papyrus
		// consumer has no name Meridian knows and should not have to acquire
		// one. Consumers stay apart the same way they already do under Prisma:
		// by the folder their own asViewPath names.
		//
		// Release browsers are pinned to a single mod:// host, so one host for
		// every view of this bridge satisfies that by construction rather than
		// by luck - and it keeps satisfying it however many views exist, which
		// a host-per-consumer scheme would not.
		constexpr const char* kUrlPrefix = "mod://Lodestone/";

		// The JS object a page's inbound bindings hang off: Lodestone.<fn>(...).
		//
		// MERIDIAN REQUIRES A NON-EMPTY OBJECT NAME - JSFuncInfo rejects a null
		// or empty one - so the bare window global that Prisma injects is not
		// directly available. Rather than make consumers write two pages, every
		// registration also gets a bare-window alias installed on page load, so
		// window.<fn>(...) works here exactly as it does under Prisma. The
		// object form is the canonical one; the alias is compatibility.
		constexpr const char* kJsObjectName = "Lodestone";

		// How often a loading view is asked whether its page is up, and how long
		// it is given before this module stops watching.
		//
		// MEASURED IN TIME, NOT IN POLLS, AND THE FIRST VERSION OF THIS GOT IT
		// WRONG IN A WAY WORTH KEEPING WRITTEN DOWN. It counted polls and
		// requeued itself through SKSE's task interface, on the assumption that
		// a task added from inside a task runs on the NEXT frame. It does not:
		// the queue is drained in a loop, and a task added during the drain is
		// picked up by the same drain. Measured in game on 2026-09-07 - a
		// budget of 1800 "frames" was spent in ONE MILLISECOND:
		//
		//   [07:39:34.671] view 'StrengthMatters' created ...
		//   [07:39:34.672] a view did not finish loading after 1800 frames
		//
		// So the view was abandoned a millisecond after being asked for, the
		// ready event never fired, and the consumer waited forever for it. The
		// page was fine and never got a chance to load.
		//
		// Two things changed. The budget is wall-clock, which is what the
		// question was always about. And the waiting is done by a thread that
		// SLEEPS, which posts one task per interval to do the actual asking -
		// because the browser is only safe to touch on the game thread, and
		// re-queuing was a busy spin on it.
		constexpr auto kReadyPollInterval = std::chrono::milliseconds(100);
		constexpr auto kReadyTimeout      = std::chrono::seconds(30);

		// Giving up does NOT destroy the view: it stops the watching. The view
		// stays in the bridge's table, WebUIGetViewState keeps answering 0
		// (created, not loaded), and the consumer can still destroy it.

		// The SKSE plugin Meridian UI ships. Its presence in the process is the
		// cheapest honest answer to "is Meridian installed", and asking before
		// acting is the whole reason this constant exists - see g_installed.
		constexpr const char* kPluginModule = "MeridianUIPlugin.dll";

		// Whether that module is loaded. Settled in Probe() and never revisited:
		// SKSE has loaded every plugin before kPostLoad, so the answer cannot
		// change afterwards.
		//
		// NOTHING HAPPENS WHEN THIS IS FALSE, AND THAT IS A BUG FIX RATHER THAN
		// AN OPTIMIZATION. Both halves of the handshake name Meridian as their
		// peer, and CommonLibSSE-NG logs when a peer is not there:
		//
		//   [error]   Failed to register messaging listener for MeridianUI
		//   [warning] Failed to dispatch message to MeridianUI
		//
		// Those come from Interfaces.cpp:304 and :285 of the pinned v6.7.1, not
		// from this plugin, so they cannot be silenced by changing what this
		// file logs - only by not making the calls. Measured in game on
		// 2026-09-07 with Meridian absent: one error and one warning on every
		// load, which is the common case for anyone who never installs it.
		//
		// That is exactly what WebUIBridge.h forbids: an absent backend is the
		// normal state, and a probe that logs a failure teaches users to report
		// a non-problem.
		bool g_installed = false;

		// Written on the game thread during load, read from the Papyrus VM
		// thread by IsAvailable(). Null means Meridian is not installed, or has
		// not answered yet, or has shut down - all three are the same answer to
		// a caller.
		std::atomic<Meridian::UI::IUIPlatformAPI*> g_api{ nullptr };

		// Handed to Meridian by pointer, with its size, during the handshake, so
		// it has to outlive the dispatch. Defaults are deliberate: the
		// RingBuffer renderer, no remote debugging port, and no remote content -
		// the last of which is what keeps every browser pinned to mod://.
		Meridian::UI::Settings g_settings{};

		// One view, as this backend knows it.
		//
		// NO LOCK, AND THAT IS A CONTRACT RATHER THAN AN OMISSION. Every entry
		// point that touches this vector - the six view operations - is called
		// by the coordinator on the main game thread, and so is the watcher
		// below. The two things that arrive on other threads, the JS thunks and
		// IsAvailable(), do not read it.
		struct BrowserRecord
		{
			IWebUIBackend::ViewHandle handle  = 0;
			Meridian::CEF::IBrowser*  browser = nullptr;

			// Whether the page is loaded RIGHT NOW. This is a state, not a
			// milestone, and the difference is the whole reason the watcher
			// keeps looking after the first success - see aliases below.
			bool loaded = false;

			// Whether ViewReady has been reported. Once only: a consumer is
			// told a view became usable, and being told twice is a change to
			// published behavior, not a bug fix.
			bool announced = false;

			// Whether the timeout has already been logged, so a view that
			// never loads says so once instead of every hundred milliseconds.
			bool gaveUp = false;

			// When the browser was asked for, which is what kReadyTimeout is
			// measured against.
			std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();

			// Every bare-window alias this view has been given, kept for the
			// life of the view rather than consumed on first use.
			//
			// RE-INSTALLED ON EVERY PAGE LOAD, AND THAT IS A BUG FIX. An alias
			// is a script, so it belongs to the document that was loaded when
			// it ran - a page load replaces that document and the alias is
			// gone. The binding underneath it survives, because the platform
			// replays AddFunctionCallback registrations on load (IBrowser.h),
			// so the two would silently drift apart: Lodestone.<fn> keeps
			// working and window.<fn> stops existing.
			//
			// That failure is invisible from the game. The page calls
			// window.<fn> and nothing happens - no error, no log line, and the
			// consumer's inbound channel is simply dead. It was found by
			// reasoning from a position reset the author reported on
			// 2026-09-07, not by anything failing loudly.
			std::vector<std::string> aliases;
		};

		std::vector<BrowserRecord> g_browsers;

		BrowserRecord* Find(IWebUIBackend::ViewHandle a_view)
		{
			for (auto& record : g_browsers) {
				if (record.handle == a_view) {
					return &record;
				}
			}
			return nullptr;
		}

		// --- Turning a Papyrus payload into JavaScript --------------------------

		// Whether a name is safe to paste into a script as an identifier.
		//
		// THE NAME COMES FROM PAPYRUS, so it is caller data, and Call() builds a
		// script out of it. Anything but a plain identifier is refused rather
		// than escaped: there is no legitimate view function named with a
		// parenthesis, and refusing is the only answer that cannot be wrong.
		bool IsSafeJsIdentifier(const char* a_name)
		{
			if (!a_name || !*a_name) {
				return false;
			}

			for (const char* p = a_name; *p; ++p) {
				const unsigned char c  = static_cast<unsigned char>(*p);
				const bool          ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
					(c >= '0' && c <= '9') || c == '_' || c == '$';
				if (!ok) {
					return false;
				}
			}

			const unsigned char first = static_cast<unsigned char>(*a_name);
			return !(first >= '0' && first <= '9');
		}

		// Wraps a payload as a JavaScript string literal.
		//
		// The bridge's contract is that the page receives the payload as ONE
		// string argument - the example view parses it with JSON.parse - so this
		// has to produce a string literal, not spliced-in JSON.
		//
		// U+2028 AND U+2029 ARE ESCAPED BY BYTE PATTERN, and they are the reason
		// this is not a three-case switch. Both are legal inside a JSON string
		// and both terminate a JavaScript string literal, so a payload carrying
		// one would produce a syntax error inside the page with nothing in any
		// log to explain it. Papyrus hands over bytes, so they are matched as
		// the UTF-8 sequences they arrive as.
		std::string ToJsStringLiteral(const char* a_raw)
		{
			std::string out;
			out.push_back('"');

			for (const unsigned char* p = reinterpret_cast<const unsigned char*>(a_raw ? a_raw : ""); *p; ++p) {
				if (p[0] == 0xE2 && p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) {
					out += (p[2] == 0xA8) ? "\\u2028" : "\\u2029";
					p += 2;
					continue;
				}

				switch (*p) {
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (*p < 0x20) {
						char buf[8]{};
						std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(*p));
						out += buf;
					} else {
						out.push_back(static_cast<char>(*p));
					}
					break;
				}
			}

			out.push_back('"');
			return out;
		}

		// Installs window.<fn> as a forwarder to Lodestone.<fn>.
		//
		// This is what makes a page written against Prisma work here unchanged.
		// Guarded on the object existing, because a page that loaded before the
		// binding was replayed would otherwise throw on the assignment.
		void InstallAlias(Meridian::CEF::IBrowser* a_browser, const std::string& a_function)
		{
			std::string script;
			script += "if (window.";
			script += kJsObjectName;
			script += " && typeof window.";
			script += kJsObjectName;
			script += ".";
			script += a_function;
			script += " === 'function') { window.";
			script += a_function;
			script += " = function () { return window.";
			script += kJsObjectName;
			script += ".";
			script += a_function;
			script += ".apply(null, arguments); }; }";

			a_browser->ExecuteJavaScript(script.c_str());
		}

		// --- Watching for the page to load --------------------------------------

		// How many live views there are. Read by the watcher thread, written on
		// the game thread.
		//
		// EVERY VIEW IS WATCHED FOR ITS WHOLE LIFE, not just until it first
		// loads, because a page can be replaced afterwards and the aliases have
		// to go back in when it is. The cost of that is two virtual calls per
		// view per hundred milliseconds - measured against a game frame, it is
		// nothing, and it buys a channel that would otherwise die in silence.
		std::atomic<int> g_watchedViews{ 0 };

		// Whether the watcher thread has been started. It is started at most
		// once per process.
		std::atomic<bool> g_watcherStarted{ false };

		// One pass over every view still loading. GAME THREAD ONLY - it is
		// posted there by the watcher thread and touches g_browsers, which
		// belongs to that thread.
		//
		// WHY ASKING AT ALL, AND IT IS NOT A SHORTCUT: the Meridian API has no
		// page-ready callback anywhere. IBrowser offers IsBrowserReady() and
		// IsPageLoaded(), both of them questions. Prisma hands the bridge a
		// callback; this file has to earn the same event.
		void PollReady()
		{
			const auto now = std::chrono::steady_clock::now();

			for (auto& record : g_browsers) {
				if (!record.browser) {
					continue;
				}

				const bool nowLoaded = record.browser->IsBrowserReady() && record.browser->IsPageLoaded();

				// The rising edge, and it happens more than once. The first one
				// is the view becoming usable; every later one is the page
				// having been replaced under us, which is when the aliases have
				// to go back in.
				if (nowLoaded && !record.loaded) {
					record.loaded = true;

					for (const auto& function : record.aliases) {
						InstallAlias(record.browser, function);
					}

					if (!record.announced) {
						record.announced = true;
						try {
							WebUIBackendCallbacks::ViewReady(WebUIBackends::MeridianUI(), record.handle);
						} catch (...) {
							spdlog::error("MeridianUIBackend: reporting a ready view threw - the view "
										  "is usable but no ready event was sent.");
						}
					} else {
						// Worth a line, because it is the only visible trace
						// that a consumer's page state was thrown away. The
						// bridge does NOT re-announce readiness here - see the
						// note on `announced`.
						spdlog::info("MeridianUIBackend: a view's page reloaded - JS bindings were "
									 "re-installed, but anything the page itself was holding is gone.");
					}
					continue;
				}

				if (!nowLoaded && record.loaded) {
					record.loaded = false;
					continue;
				}

				if (!record.announced && !record.gaveUp && now - record.createdAt >= kReadyTimeout) {
					record.gaveUp = true;
					spdlog::error("MeridianUIBackend: a view did not finish loading after {} seconds - "
								  "no ready event will be sent for it. Check that the page exists "
								  "under Data\\MeridianUI\\Lodestone.",
						std::chrono::duration_cast<std::chrono::seconds>(kReadyTimeout).count());
				}
			}
		}

		// Sleeps, and posts one PollReady to the game thread per interval while
		// anything is loading.
		//
		// IT NEVER EXITS, AND THAT IS THE CHEAPER OF TWO CORRECT ANSWERS. A
		// thread that stops when the last view is ready and restarts on the next
		// create has a window where a create lands just as the thread decides to
		// leave, and the view is then never watched. Closing that costs a
		// compare-exchange dance; keeping the thread costs one atomic read every
		// hundred milliseconds, for the rest of the session, and only in a
		// process where Meridian is installed AND a view was actually created.
		//
		// It touches nothing but the two atomics and the task interface. All
		// browser access happens inside the posted task, on the game thread.
		void WatcherLoop()
		{
			for (;;) {
				std::this_thread::sleep_for(kReadyPollInterval);

				if (g_watchedViews.load(std::memory_order_acquire) <= 0) {
					continue;
				}

				// Shutting down. Nothing may touch a browser after this, and
				// there is nothing left to report.
				if (!g_api.load(std::memory_order_acquire)) {
					continue;
				}

				if (auto* task = SKSE::GetTaskInterface()) {
					task->AddTask([]() { PollReady(); });
				}
			}
		}

		void StartWatcher()
		{
			bool expected = false;
			if (g_watcherStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
				std::thread(&WatcherLoop).detach();
			}
		}

		// --- JS listener thunks -------------------------------------------------
		//
		// Meridian's JSFuncCallback is void(*)(const char** args, int count) -
		// like Prisma's, it carries NO context argument, so a capturing lambda
		// cannot be handed over and the slot has to be baked into the function's
		// type. Same table, same reason, different signature.
		//
		// executeInGameThread is left at its default of true (JSTypes.h:15), so
		// these arrive on the game thread rather than a renderer thread. That
		// does NOT let this file skip the bridge's task queue: SendModEvent
		// still goes through it, because being on the game thread is a property
		// of this backend and not of the bridge's contract.
		//
		// Wrapped because they return into Meridian.
		template <std::size_t N>
		void ListenerThunk(const char** a_args, int a_argsCount)
		{
			try {
				const char* argument = (a_args && a_argsCount > 0) ? a_args[0] : nullptr;
				WebUIBackendCallbacks::ListenerFired(N, argument);
			} catch (...) {
				spdlog::error("MeridianUIBackend: a JS listener threw on slot {} - the mod event was "
							  "not sent.",
					N);
			}
		}

		template <std::size_t... I>
		constexpr std::array<Meridian::JS::JSFuncCallback, sizeof...(I)> MakeThunkTable(std::index_sequence<I...>)
		{
			return { &ListenerThunk<I>... };
		}

		const auto g_thunks = MakeThunkTable(std::make_index_sequence<kWebUIMaxListeners>{});

		// The names Meridian holds by pointer. JSFuncInfo takes const char* and
		// the platform keeps the binding past the call, so the strings backing
		// those pointers have to outlive it.
		std::array<std::string, kWebUIMaxListeners> g_slotNames{};

		// --- The API arriving ---------------------------------------------------

		void OnShutdown()
		{
			// "After this callback, you should stop using any browser" - API.h.
			// Dropping the pointer makes IsAvailable() false, and every native
			// then answers its sentinel instead of touching a dead browser.
			g_api.store(nullptr, std::memory_order_release);
			g_browsers.clear();
			spdlog::info("MeridianUIBackend: Meridian UI is shutting down - bridge inactive.");
		}

		void OnApiReady(Meridian::UI::IUIPlatformAPI* a_api)
		{
			if (!a_api) {
				return;
			}

			a_api->RegisterOnShutdown(&OnShutdown);
			g_api.store(a_api, std::memory_order_release);
		}

		// --- The backend --------------------------------------------------------

		class MeridianUIBackend final : public IWebUIBackend
		{
		public:
			const char* Name() const override { return "MeridianUI"; }
			const char* DisplayName() const override { return "Meridian UI"; }
			const char* ViewRootHint() const override { return "Data\\MeridianUI\\Lodestone for Meridian UI"; }

			// Arms the handshake and nothing else.
			//
			// NO POINTER EXISTS YET WHEN THIS RETURNS, and that is the whole
			// difference from Prisma. This registers the listener that will
			// receive Meridian's replies; the requests that provoke them go out
			// from HandleSKSEMessage below, at kPostPostLoad and kInputLoaded.
			void Probe() override
			{
				// Ask whether Meridian is here before saying its name to
				// anything. See g_installed for the two log lines this silences
				// and why they are not ours to silence any other way.
				g_installed = REX::W32::GetModuleHandleA(kPluginModule) != nullptr;
				if (!g_installed) {
					return;
				}

				Meridian::UI::SKSELoader::GetUIPlatformAPIWithVersionCheck(&OnApiReady);
			}

			// The loader's own dispatcher, which owns both halves of the
			// handshake: it sends RequestVersion at kPostPostLoad and RequestAPI
			// at kInputLoaded, and it checks the reply's version against the
			// headers this DLL was built with.
			//
			// IT HAS TO BE CALLED FROM HERE and not only from the listener that
			// Probe() registered. That listener is registered for messages whose
			// sender is "MeridianUI", so it never sees an SKSE lifecycle message
			// - the two branches inside ProcessSKSEMessage are fed from two
			// different places by design.
			void HandleSKSEMessage(SKSE::MessagingInterface::Message* a_msg) override
			{
				// Same guard as Probe(), and for the same reason: the dispatch
				// inside names Meridian as the receiver, and a receiver that is
				// not loaded is a warning in the user's log.
				if (!g_installed) {
					return;
				}

				Meridian::UI::SKSELoader::ProcessSKSEMessage(a_msg, &g_settings);
			}

			bool IsAvailable() const override
			{
				return g_api.load(std::memory_order_acquire) != nullptr;
			}

			bool HasCapability(const char* a_capability) const override
			{
				if (!a_capability) {
					return false;
				}

				const std::string_view capability(a_capability);

				// "focus-stack": can two views hold focus independently.
				//
				// False, and for a different reason than Prisma's. Meridian
				// arbitrates rather than failing to stack: exactly one browser
				// holds focus at a time across every consumer, by design. A
				// consumer asking this wants to know whether it can rely on two
				// views being independently focusable, and the answer is no on
				// both backends - which is the point of asking about the
				// capability instead of the backend's name.
				if (capability == "focus-stack") {
					return false;
				}

				// "view-order": IBrowser::SetBrowserZOrder, higher draws on top.
				if (capability == "view-order") {
					return true;
				}

				// "inspector": a developer inspector opened on a view, the way
				// Prisma's CreateInspectorView does.
				//
				// False. Meridian's Settings::remoteDebuggingPort attaches an
				// external CEF debugger over a port; that is a different thing
				// from opening an inspector on a view, and answering True would
				// promise a consumer something it cannot get from this API.
				if (capability == "inspector") {
					return false;
				}

				return false;
			}

			ViewHandle CreateView(const char* a_viewId, const char* a_viewPath) override
			{
				auto* api = g_api.load(std::memory_order_acquire);
				if (!api || !a_viewId || !*a_viewId || !a_viewPath || !*a_viewPath) {
					return 0;
				}

				// THE VIEW PATH IS USED AS GIVEN, and the view id does NOT enter
				// the URL. It is the browser's unique name and nothing else.
				//
				// This is what makes one page work on both backends: the same
				// asViewPath resolves under each backend's own root, so a
				// consumer ships the identical folder to two places and passes
				// the identical string.
				//
				//   Prisma UI    Data\PrismaUI\views\<asViewPath>
				//   Meridian UI  Data\MeridianUI\Lodestone\<asViewPath>
				//
				// Putting the id in the path as well would double the
				// consumer's own folder - the Strength Matters view id is
				// "StrengthMatters" and its path is "StrengthMatters/index.html"
				// - and would silently break every path that already works.
				std::string url = kUrlPrefix;
				url += a_viewPath;

				Meridian::CEF::IBrowser* browser = nullptr;

				// No bindings at creation: the bridge registers listeners later,
				// when a consumer asks, and AddFunctionCallback handles that.
				const auto handle = api->AddOrGetBrowser(a_viewId, nullptr, 0, url.c_str(), browser);

				if (handle == Meridian::UI::IUIPlatformAPI::InvalidBrowserRefHandle || !browser) {
					return 0;
				}

				g_browsers.push_back(BrowserRecord{ static_cast<ViewHandle>(handle), browser });

				// HIDDEN ON CREATE, AND THIS WAS MEASURED RATHER THAN ASSUMED.
				// Meridian does not document whether a browser starts visible.
				// The first version of this file asked and logged the answer
				// instead of guessing, and in game on 2026-09-07 the answer came
				// back TRUE - a browser is visible the moment it exists, and
				// with no rect set it is fullscreen. So a view that a consumer
				// created but has not shown yet would paint over the whole
				// screen as soon as its page loaded.
				//
				// Hiding here makes "created but not shown" mean the same thing
				// on both backends, which is what the .psc promises: a view
				// becomes visible when WebUIShow is called, not when it is
				// built.
				const bool visibleAtCreation = browser->IsBrowserVisible();
				browser->SetBrowserVisible(false);

				spdlog::info("MeridianUIBackend: view '{}' created from '{}' - hidden until WebUIShow "
							 "(the backend made it visible at creation: {}).",
					a_viewId, url, visibleAtCreation);

				g_watchedViews.fetch_add(1, std::memory_order_acq_rel);
				StartWatcher();
				return static_cast<ViewHandle>(handle);
			}

			void DestroyView(ViewHandle a_view) override
			{
				auto* api = g_api.load(std::memory_order_acquire);
				if (!api) {
					return;
				}

				for (std::size_t i = 0; i < g_browsers.size(); ++i) {
					if (g_browsers[i].handle == a_view) {
						// The view stops being watched when it stops existing,
						// and not before - see g_watchedViews.
						g_watchedViews.fetch_sub(1, std::memory_order_acq_rel);
						g_browsers.erase(g_browsers.begin() + static_cast<std::ptrdiff_t>(i));
						break;
					}
				}

				api->ReleaseBrowserHandle(static_cast<Meridian::UI::IUIPlatformAPI::BrowserRefHandle>(a_view));
			}

			void Show(ViewHandle a_view) override
			{
				if (auto* record = Find(a_view); record && record->browser) {
					record->browser->SetBrowserVisible(true);
				}
			}

			void Hide(ViewHandle a_view) override
			{
				if (auto* record = Find(a_view); record && record->browser) {
					record->browser->SetBrowserVisible(false);
				}
			}

			// ExecuteJavaScript, and NOT ExecEventFunction, which looks like the
			// closer fit and is not usable here.
			//
			// An event function has to be declared in the JSFuncInfo array at
			// creation, with isEventFunction set. The bridge does not know any
			// function name at creation: a consumer names one when it calls
			// WebUICall, arbitrarily far into the session. Declaring names that
			// have not been chosen yet is not possible, so the general path is
			// the only one, and it produces the same thing the .psc promises -
			// calling that name on the page, with the payload as its single
			// string argument.
			void Call(ViewHandle a_view, const char* a_function, const char* a_json) override
			{
				auto* record = Find(a_view);
				if (!record || !record->browser) {
					return;
				}

				if (!IsSafeJsIdentifier(a_function)) {
					spdlog::error("MeridianUIBackend: refusing to call '{}' - a view function name has "
								  "to be a plain JavaScript identifier.",
						a_function ? a_function : "");
					return;
				}

				std::string script = "if (typeof window.";
				script += a_function;
				script += " === 'function') { window.";
				script += a_function;
				script += "(";
				script += ToJsStringLiteral(a_json);
				script += "); }";

				record->browser->ExecuteJavaScript(script.c_str());
			}

			void RegisterListener(ViewHandle a_view, const char* a_jsFunction, std::size_t a_slot) override
			{
				auto* record = Find(a_view);
				if (!record || !record->browser || a_slot >= g_thunks.size()) {
					return;
				}

				if (!IsSafeJsIdentifier(a_jsFunction)) {
					spdlog::error("MeridianUIBackend: refusing to bind '{}' - a JS function name has to "
								  "be a plain JavaScript identifier.",
						a_jsFunction ? a_jsFunction : "");
					return;
				}

				// Meridian holds these by pointer, so the strings have to
				// outlive the call. One per slot, overwritten when the slot is
				// reused, which is safe because the bridge frees a slot only
				// after the binding it named is gone with its view.
				g_slotNames[a_slot] = a_jsFunction;

				Meridian::JS::JSFuncInfo info{};
				info.objectName                       = kJsObjectName;
				info.funcName                         = g_slotNames[a_slot].c_str();
				info.callbackData.callback            = g_thunks[a_slot];
				info.callbackData.executeInGameThread = true;
				info.callbackData.isEventFunction     = false;

				record->browser->AddFunctionCallback(info);

				// The bare-window alias that makes a Prisma-shaped page work
				// here. Remembered for the life of the view, because it is a
				// script and every page load throws it away - the watcher puts
				// it back on each one. Installed now as well if there is already
				// a document to install it into.
				if (std::find(record->aliases.begin(), record->aliases.end(), g_slotNames[a_slot]) ==
					record->aliases.end()) {
					record->aliases.push_back(g_slotNames[a_slot]);
				}

				if (record->loaded) {
					InstallAlias(record->browser, g_slotNames[a_slot]);
				}
			}
		};

		MeridianUIBackend g_backend;
	}

	namespace WebUIBackends
	{
		IWebUIBackend* MeridianUI()
		{
			return &g_backend;
		}
	}
}
