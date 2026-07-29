# resolume-luma-keyer

Luma keyer FFGL effect plugin for Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. Public repo, released v1.0.0.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build`
- Output: FFGL `.bundle` (macOS, universal) / `.dll` (Windows).

## Notes
- FFGL plugin — the effect runs as GLSL in Resolume's render pipeline; the C++ side is the FFGL host glue.
- macOS build must be universal (arm64 + x86_64) so it loads in both Resolume builds.
- Public repo. "Commit" = commit **and** push.
