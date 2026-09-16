// MenuPrompt.h
// Lodestone - Shared SKSE framework
//
// Module: MenuPrompt (Core)
//
// Asks the player a question through an on-screen menu and hands the answer
// back to Papyrus. Two questions, which are the two a consumer actually needs:
// pick one of N texts, and type a line of text.
//
// Papyrus-facing script: Lodestone.psc
//
// Phase L-U8
//
// ---------------------------------------------------------------------------
// NAMED FOR WHAT IT DOES, NOT FOR WHO SUPPLIES IT
// ---------------------------------------------------------------------------
//
// The menus are drawn by SKSE Menu Framework, and that name appears nowhere in
// the Papyrus surface. This is the lesson Core/WebUIBridge paid for in 1.18.0
// and wrote at the top of its own header: a contract that names its supplier
// cannot outlive it. The natives say "list" and "text entry".
//
// It is deliberately NOT a third IWebUIBackend. That seam is about pages -
// a view, an HTML root, a JavaScript call, a DOM that becomes ready. None of
// those words mean anything to an immediate-mode menu, and forcing this
// through that interface would widen it for one implementation that fits none
// of it.
//
// ---------------------------------------------------------------------------
// INACTIVE IS NOT BROKEN, AND THE LOG SAYS WHICH
// ---------------------------------------------------------------------------
//
// The framework is OPTIONAL and is not a dependency of Lodestone. With it
// absent every function here returns its sentinel, nothing is written at error
// level, and one line at load says which state the module ended up in. A
// consumer asking whether the surface is available is expecting False as a
// normal answer, not as a failure - so False costs nothing and logs nothing.
//
// Nothing inside Lodestone consumes this. It is exposed, never depended on.
//
// ---------------------------------------------------------------------------
// ONE PROMPT AT A TIME, AND A REFUSAL IS NOT A CANCELLATION
// ---------------------------------------------------------------------------
//
// A request made while another prompt is open is REFUSED, not queued - the
// same rule Core/WebUIBridge applies to view focus, for the same reason: a
// queue makes a script wait on a window it never asked for and cannot see.
//
// The caller must be able to tell three outcomes apart, and the Papyrus
// surface keeps them distinct:
//
//   picked / accepted   the player answered
//   cancelled           the player dismissed the prompt, or a load took it away
//   refused             another prompt was already open; nothing was shown
//
// Empty text is a fourth thing, and it is an ANSWER: the player cleared the
// box and accepted. The UIExtensions surface this replaces cannot express that
// - it reports empty and cancelled identically - and consumers grew code that
// treats empty as cancel because that was all they could do.
//
// ---------------------------------------------------------------------------
// THE PAPYRUS CALL WAITS, AND THAT IS A LATENT NATIVE
// ---------------------------------------------------------------------------
//
// The surface this replaces blocks its caller, and the consumers written
// against it depend on that: one of them reopens a list in a loop until the
// player is finished. So the natives here park the calling script and answer it
// when the window closes.
//
// Two ways to do that were open, and the choice was made on evidence rather
// than on preference:
//
//   latent native            the VM parks the stack, the plugin answers later
//   open + poll from Papyrus a script loop around Utility.Wait
//
// THE SECOND ONE RESTS ON SOMETHING NOT MEASURED. The surface being replaced
// only works because Utility.Wait stops advancing while a vanilla menu is up
// (read first-hand in uimenubase.psc and uilistmenu.psc:133-156, a Wait(0.1)
// loop capped at 50 turns). The window here is not a vanilla menu and its pause
// is the framework's own, so a poll loop would depend on an interaction nobody
// has measured. The latent native depends on nothing unmeasured.
//
// The non-blocking query the second design would have given is exposed anyway -
// see IsBusy - because it costs nothing and a consumer may not want to wait.
//
// The latent natives are NOT registered through CommonLib's
// RegisterLatentFunction: in the pinned submodule that template does not
// compile for any result type. MenuPrompt.cpp binds them with a local class
// over the same base, and says exactly what is broken and where.
//
// ---------------------------------------------------------------------------
// THE RENDER CALLBACK IS A FOREIGN THREAD, AND SO ARE THE OTHER TWO
// ---------------------------------------------------------------------------
//
// Measured in game over three runs (phase probe, 2026-09-15): the render
// callback ran on the same thread as an SKSE task in all three, but the input
// and event callbacks had NO fixed thread - a different id almost every time.
// So every piece of state shared between them is atomic or held under the
// lock, and the render callback touches no form, no VM and no game object.
//
// A result travels back to Papyrus through the SKSE task interface, which is
// what makes it the game thread - the same DispatchToGame shape Core/WebUIBridge
// uses. Note that AddTask does NOT defer to the next frame; it is used here to
// change THREAD, never to wait.
//
// Nothing throws out of a callback: they are called from inside the framework,
// and an exception crossing back into a vendor's frame is the same undefined
// behaviour as one crossing into the VM.
//
// ---------------------------------------------------------------------------
// ESCAPE IS OURS TO HANDLE, WHICH IS NOT WHAT THE FRAMEWORK'S PAGE IMPLIES
// ---------------------------------------------------------------------------
//
// Measured, not assumed: Escape does NOT close a window this plugin opens.
// ImGui sees the key - the probe logged it 14 times - and the window's IsOpen
// stays true. The same was true of another mod's window in the same session,
// so it is the framework's behaviour and not this module's mistake. The render
// callback therefore reads Escape itself and treats it as cancellation.
//
// The framework's own hotkey does not close these windows either: it opens the
// framework's panel OVER them without touching IsOpen.
//
// AND DO NOT WIRE LOGIC TO ITS OPEN/CLOSE EVENTS. In three measured runs the
// open event never fired at all, and the close event fired 135 times in bursts
// with windows both open and closed, from several threads. The names do not
// describe what those events do in this installation, so this module observes
// IsOpen directly instead.
//
// ---------------------------------------------------------------------------
// A LOAD TAKES THE PROMPT AWAY, AND THE SCRIPT IS TOLD
// ---------------------------------------------------------------------------
//
// A save being loaded or a new game starting closes an open prompt and
// completes it as a cancellation. A script waiting on a window that belongs to
// a world which no longer exists is the state to avoid, and it cannot be fixed
// from the consumer's side.
//
// ---------------------------------------------------------------------------
// TEXT IS UTF-8, AND ITS LENGTH IS IN BYTES
// ---------------------------------------------------------------------------
//
// Measured: a 15-character line with one accented letter came back with a
// length of 16. The edit buffer is fixed, so a suggestion longer than it is
// truncated - and truncation walks back to a character boundary, because
// cutting a multi-byte character in half produces text the VM should never be
// handed.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Lodestone::Core::MenuPrompt
{
	// What a finished prompt produced. `accepted` false is a cancellation,
	// whatever caused it - the player, Escape, or a load.
	//
	// `index` is meaningful for a list prompt only, `text` for a text prompt
	// only. An accepted text prompt may carry an empty string, and that is an
	// answer rather than a cancellation.
	struct Result
	{
		bool        accepted{ false };
		int         index{ -1 };
		std::string text;
	};

	// Called once when a prompt finishes, ON THE GAME THREAD. Never called for
	// a request that was refused - a refusal is reported by Begin* returning
	// false, before anything is shown.
	using Completion = std::function<void(Result)>;

	// Whether prompts can be shown at all: the framework is installed AND its
	// module is loaded AND the windows registered.
	//
	// A probe, not a failure. False is the expected answer on most load orders
	// and writes nothing to the log. Cannot fail.
	bool Available();

	// Whether a prompt is open right now. A Begin* call would be refused.
	bool IsBusy();

	// Shows a single-choice list. Returns false if nothing was shown - either
	// the surface is unavailable or another prompt is open - and in that case
	// a_done is never called.
	//
	// There is no entry limit. The list is drawn through a clipper, so cost
	// follows what is on screen rather than what is in the vector.
	bool BeginList(std::string a_title, std::vector<std::string> a_entries, Completion a_done);

	// Shows a text entry box seeded with a_suggestion, selected, so that typing
	// replaces it and Enter accepts it unchanged.
	bool BeginText(std::string a_title, std::string a_suggestion, Completion a_done);

	// Registers the two windows with the framework, once. Called from
	// plugin.cpp on kDataLoaded - the seam the framework's own consumers use,
	// and late enough that its DLL is loaded.
	//
	// Cannot fail in a way a caller can act on. Every path logs which state the
	// module ended in, because from the outside "no framework" and "broken" are
	// indistinguishable.
	void Install();

	// Closes and cancels an open prompt when a save is loaded or a new game
	// starts. Driven from plugin.cpp's message handler.
	void HandleSKSEMessage(SKSE::MessagingInterface::Message* a_msg);

	// Registers this module's native functions with the Papyrus VM.
	// Called by Lodestone::Core::Papyrus::Register - never called directly.
	//
	// Returns false if any registration failed.
	bool RegisterFuncs(RE::BSScript::IVirtualMachine* a_vm);
}
