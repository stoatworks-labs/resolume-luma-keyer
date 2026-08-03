# resolume-luma-keyer

Luma keyer FFGL effect plugin for Resolume Arena/Avenue. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. Public repo, released v1.0.1.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Build: `cmake --build build`
- Output: FFGL `.bundle` (macOS, universal) / `.dll` (Windows), plus the OpenFX
  build `build/LumaKey.ofx.bundle` (target `LumaKeyOFX`, `-DBUILD_OFX=OFF` to skip).
- OFX smoke test (uses the bridge's host):
  `../resolume-ofx-bridge/build/ofxprobe --dir build --render com.stoatworks.lumakey`

## After Effects / Premiere build (adobe/)
- Rust, on the `after-effects` crate (pinned git rev) — no Adobe SDK download,
  the crate ships pre-generated bindings and an AE plugin links against nothing.
- Build + bundle: `cd adobe && ./bundle.sh` (debug) or `./bundle.sh release`
  (universal `target/release/LumaKey.plugin`).
- Install (needs sudo — both Adobe plugin folders are root-owned):
  `sudo cp -R adobe/target/release/LumaKey.plugin "/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"`
  — that one folder serves BOTH After Effects and Premiere.
- In-host test (AE must be running): `adobe/test/smoke.jsx` via
  `osascript -e 'tell application "Adobe After Effects 2026" to DoScriptFile ...'`,
  then inspect /tmp/lumakey-ae-smoke.png (dark half red, bright half grey).
- The key maths now has THREE homes: the GLSL, the OFX C++, and adobe/src/lib.rs.
  Edit one, edit all three.
- Match name `STWK Luma Key` is the serialisation key — never change it.
- Premiere's legacy render path is BGRA where AE's is ARGB; the luminance
  weights are swapped when the host reports PrMr. See lib.rs.

## OpenFX build
- `source/ofx/LumaKeyOFX.cpp` is the same key on the CPU for Resolve/Nuke/Natron/Vegas;
  the GLSL in `source/LumaKey.cpp` and the CPU math must key identically — edit both.
- OFX SDK subset (C headers + Support library, BSD-3) vendored under `external/openfx`.
- Install for Resolve: copy `LumaKey.ofx.bundle` into `/Library/OFX/Plugins`.

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
