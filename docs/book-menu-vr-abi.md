# Book opening ABI on Skyrim VR

`BookFramework` hooks the native function behind
`BookMenu::OpenMenu_Impl`. SE/AE use eight arguments. Skyrim VR adds a
ninth argument, `RE::NiAVObject*`, identifying the source scene object.
The hook must receive and forward it unchanged, including null, on every
path through book-text substitution.

On Windows x64 the ninth argument occupies caller stack slot
`[rsp+0x40]`, or `[rsp+0x48]` after CALL pushes the return address.
Forwarding only eight arguments leaves that slot unspecified. The engine
can then treat an unrelated value as the scene pointer it retains until
the book menu closes.

Select the native eight- or nine-argument hook when installing the detour.
Share text substitution between the two signatures and preserve all
arguments on both success and fallback. No scene-object dereference or
ownership operation belongs in the hook. Supplying null unconditionally
would discard the native caller's source-object state.

The standalone regression under `tests/book-framework` exercises the
production forwarding code with both signatures. It does not install a
real SafetyHook detour or validate game object lifetime. An in-game retest
should cover world and inventory books, repeated opening/closing, and
replacement text using the rebuilt DLL.
