# extern/skse-menu-framework

This folder holds one file that is **not in the repository**: `SKSEMenuFramework.h`, the C++ client header of [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) by Thiago. The build needs it to compile the menu module; the shipped DLL contains nothing of it, and the framework itself is never a dependency - it is detected at runtime and the module stays inert when it is absent.

Copy it here before building, from the author's own repository:

- `resources/SKSEMenuFramework.h` in [QTR-Modding/SKSE-Menu-Framework-3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3), branch `master`. That project's own README names this as the header consumers take - **not** `include/SKSEMenuFramework.h`, which is the framework's build-side file and a different thing.

The file is listed in `.gitignore`, so a copy placed here stays out of commits.

## The license, read and recorded

The same header is published on its own in [QTR-Modding/SKSE-Menu-Framework-3-API](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API), a repository holding exactly two files - `LICENSE` and `SKSEMenuFramework.h` - under **LGPL-2.1**. The framework's own repository is GPL-3.0 and states the split in its README: the header file has a license that does not require mods using it as a library to be open source under that same license.

The separation is therefore the upstream author's declared intent, not an inference drawn here. LGPL-2.1 adds no restriction this GPL-3.0-only repository could not carry, so - unlike `extern/prismaui-api/` - the file is kept out of the repository for a practical reason rather than a licensing one: it is a single 530 KB third-party file that its author distributes separately and revises on their own schedule, and vendoring it would fork it.

## The copy this build used

| Field | Value |
|---|---|
| Size | 530337 bytes |
| sha256, first 16 hex | `48416E8220CA777E` |
| Lines | 11213 |
| Origin | `QTR-Modding/SKSE-Menu-Framework-3`, `master`, commit `3a65dc0147388d...`, `resources/` |

Two sibling projects in this tree carry the same header, and one of them differs in size without differing in content: the copy at 519124 bytes is byte-for-byte this file with LF line endings instead of CRLF. The 11213-byte gap is exactly the line count. Do not read that difference as a version difference.

## There is no header at the runtime's version, and that is not an oversight

The dev instance runs **SMF 3.14.1**. No 3.14.1 header exists anywhere: the newest header artifact on the Nexus page is **v3.11** (2026-07-05), older than this copy, and that page has direct download disabled. The header ships with the source repository, not with the runtime mod - the installed mod folder contains only the DLL, its INI, fonts and themes.

What matters is whether this header matches the installed runtime, and that was measured rather than assumed. Against `SKSEMenuFramework.dll` 3.14.1:

- the DLL exports **1424** names;
- the header resolves **1399** names through `GetProcAddress`;
- exactly **one** name the header asks for is absent from the DLL: **`AddWindowWithView`**. Every other one resolves.

`AddWindowWithView` is the only hazard, and it is a narrow one. The header resolves each symbol in its own function-local `static`, so a missing export costs nothing until that specific function is called - and `AddWindow` does not route through it. **Do not call `AddWindowWithView` against this runtime**: it would resolve to `nullptr` and the header's null guard returns `nullptr` rather than crashing, but the window would silently never exist.

The 25 remaining exports the header never asks for are unused surface, including `RegisterEvent` (the header uses `RegisterEventPriority`) and several ImGui varargs entry points.

Re-measure this table when the runtime version changes. The measurement is a `dumpbin /exports` of the installed DLL compared against every `GetFunction<...>("name")` in this header.

## What this header needs from the rest of the build

It does not include what it uses. `IsInstalled()` calls `std::filesystem::exists` and one typedef names `RE::InputEvent`, while the header's own includes are only `<windows.h>`, `<codecvt>`, `<locale>` and `<string>`. It therefore parses only after CommonLibSSE has been seen, which `src/PCH.h` guarantees because CMake force-includes the PCH in every file of the target. Including this header from a file that somehow bypasses the PCH would fail to compile, and the error would point here rather than at the include order.

## What was measured against which version

The phase's viability probe was built against this exact copy and run in game three times against **SMF 3.13-Hotfix2**, which is what the dev instance had at the time. The behavioural findings from those runs - what Esc does, which callbacks run on which thread, how `kCloseMenu` fires - were measured on 3.13, while the export comparison above was measured against 3.14.1. Treat the behavioural findings as carrying that caveat until something re-measures them.
