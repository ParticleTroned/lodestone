# Meridian UI - API headers (vendored)

The public API headers of Meridian UI, vendored so this repository builds
without the Meridian UI mod installed. Nothing here is ours: do not edit these
files. To move to a newer Meridian UI, replace the whole folder and regenerate
the table below.

## License - MIT, and that is why these are tracked

`LICENSE-MIT` sits alongside, as the license requires. Meridian UI's own
`LICENSING.md` states it: the implementation and the shipped DLL/EXE are
GPL-3.0-or-later with exceptions, while "the standalone headers in
`src/UIPlatform/MeridianUIAPI/`, and the copies shipped in
`MeridianUI/SDK/MeridianUIAPI/`, are separately available under the MIT
License". MIT adds no restriction, so these headers can live inside this
GPL-3.0-only repository.

Contrast with `extern/prismaui-api/`, which is NOT tracked: the Prisma UI
License is proprietary and source-available, and adds restrictions this
repository cannot carry.

The MIT grant covers the header code only. It does not relicense Meridian UI's
implementation, CommonLibSSE-NG, or anything a consumer links.

## Meridian UI is not a dependency

Neither Meridian UI nor Prisma UI is required. Both are detected at runtime;
with neither installed the WebUI bridge answers sentinels and logs nothing.
These headers are a compile-time interface, not a runtime requirement.

## Version

    LibVersion   1.2.1      (Version.h - Meridian::UI::LibVersion)
    APIVersion   1.0        (Version.h - Meridian::UI::APIVersion)

The author guide shipped with the package is numbered 1.1; the header says the
API is 1.0. The header wins.

## Origin, and the one difference from the package

Copied from the Nexus release package, mod 190723, version 1.2.1.0 (from
`meta.ini` of the installed mod), folder `MeridianUI/SDK/MeridianUIAPI/`.

**The package ships CRLF; upstream and this copy are LF.** That is the only
difference, and it was measured rather than assumed: for all 17 files the
package byte count exceeds upstream's by exactly that file's CRLF count, and
after normalizing CRLF to LF the git blob SHA-1 of each local file equals the
one GitHub reports for `src/UIPlatform/MeridianUIAPI/` in
`github.com/heathbrownkeyworks/MeridianUI` - 17 of 17, no content difference.

So the bytes in this folder are the upstream repository's bytes, and the
`sha256` column below is a claim anyone can re-check.

Caveat on that comparison: it was read on 2026-09-07 against the repository's
default branch tip through the GitHub contents API, not against a commit tagged
for the 1.2.1 release. Content matched exactly; the revision was not pinned.

## What this project actually uses

Six of the sixteen headers. The other ten are the `View`, `RenderLayer`,
`NifView` and `NifScene` APIs, which the bridge does not touch. They are kept
anyway: this is one MIT-licensed block under one license file, and dropping
files out of it would create a variant nobody else has.

    API.h          IUIPlatformAPI, APIMessageType - the platform entry point
    IBrowser.h     IBrowser - per-view operations
    JSTypes.h      JSFuncInfo and the JS <-> C++ callback types
    Settings.h     Settings / BrowserSettings passed on acquisition
    SKSELoader.h   the two-step SKSE message handshake
    Version.h      LibVersion / APIVersion and the compatibility rule

## Files, as vendored

`sha256` is over the file as it sits here (LF), full digest.

| File | Bytes | sha256 |
|---|---|---|
| `API.h` | 5118 | `a19a7f69249def4dd507e033b3ed0a2914dfb91ddda8ad18d819bc79a4afb8ba` |
| `DllLoader.h` | 2342 | `5f043b0591b4d38fed15eb94befc76c76d4a59fe84bd5697bc862cd4ed73646b` |
| `IBrowser.h` | 5905 | `95959721d0bcf03f71b9ef1b657ace294907301eff27b20b06b960d216d41fb3` |
| `JSTypes.h` | 3747 | `e06213944aec5bb5699e38dca45424a3ac31637903a298e5fefa06a966c1bd6c` |
| `LICENSE-MIT` | 1142 | `1a68bbad6ff1d45dc1cdb9aa51bea0db0a2ca8ce7665a152d5596ab603e6b790` |
| `NifSceneAPI.h` | 8205 | `64036060620062336517a9160de721235ed5e1de6befd74ca773d348b3498804` |
| `NifSceneDllLoader.h` | 3446 | `9871ae7aedb588e4f8ed5d153700dd486f0e87afe2a5dbc8518944418c9bf990` |
| `NifViewAPI.h` | 3540 | `95ccc55aa16ccec71dc9aeff89d65aa2faa30aa16b730d6bd825b15597de8a4b` |
| `NifViewDllLoader.h` | 1007 | `c6836c8a01d1515eccad93f7142b31db2fefdd2f234426df569e1de6317b9eb2` |
| `RenderLayerAPI.h` | 2591 | `4698fc4f1ced4030b51b3e8b294c6770592192d1e010dae74e79a9bbf50f2d27` |
| `RenderLayerDllLoader.h` | 1023 | `48b995f9768b799befe0fa19e8578251eff297596bdfc673e909981c31c91989` |
| `SKSELoader.h` | 4566 | `b5c748789edb0f8483c502e4ab2ee5720cf4654fe88b2f695002417d56f02cb1` |
| `Settings.h` | 3440 | `dc818d0b492b996b6d522a2cd8300c0bc8925310f175e006b326f5713d2918e6` |
| `SettingsIngest.h` | 1905 | `227368bdc402da670497520921d117d112f9f1bea9175da3880eb3e5f7805453` |
| `Version.h` | 1773 | `bf1bfb2ab8af03a9420ba97fa5acbd3945a56f2339a820def76ec23c0c18640e` |
| `ViewAPI.h` | 3262 | `f03baebc538d6f5305547b0c0d7b9e7678ce2bfeacef12ed5cbab389c14f89c5` |
| `ViewDllLoader.h` | 995 | `660d0fac5151db4b9dac2ca0748a61d80f34605204570c49a9eb9214e158dfd0` |
