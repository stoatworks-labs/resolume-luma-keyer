# resolume-luma-keyer

Luma keyer FFGL effect plugin for Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. Public repo, released v1.0.1.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build`
- Output: FFGL `.bundle` (macOS, universal) / `.dll` (Windows).

## Notes
- FFGL plugin — the effect runs as GLSL in Resolume's render pipeline; the C++ side is the FFGL host glue.
- macOS build must be universal (arm64 + x86_64) so it loads in both Resolume builds.
- Public repo. "Commit" = commit **and** push.

## Diagnostics

`source/Diag.{h,cpp}` is the smallest member of the fleet's `diag` family: a log file only.
No crash handler (this runs inside Resolume) and no bundle command (an effect is three
sliders in someone else's inspector). What it does cover is the failure that actually
happens — `InitGL` returning `FF_FAIL` because a shader would not compile, which otherwise
looks like 'the effect does nothing' with no message anywhere. The GL vendor/renderer/version
strings are logged next to it, because that is almost always the reason.
