# Notes

Working notes for this repo: status, decisions, and the traps that have actually bitten.
Migrated out of Claude Code's memory on 2026-08-24, so they are written in the first
person and dated by when each thing was learned — that date is usually the useful part.

Cross-cutting notes that are not specific to this repo live in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).

*Luma keyer FFGL effect plugin for Resolume Arena/Avenue (C++/GLSL), ~/Projects/resolume-luma-keyer, GitHub public, released v1.0.0*

Simple luminance keyer effect plugin for Resolume Arena/Avenue, built on the official Resolume FFGL SDK (github.com/resolume/ffgl, vendored as a git submodule at external/ffgl). Effect ID `LK01`, name "Luma Key". Rec.709 luma → alpha with Threshold, Softness (0..1 sliders) and Invert (bool) params. Fragment shader unpremultiplies → smoothstep key → re-premultiplies with LDR clamp.

- ~/Projects/resolume-luma-keyer, GitHub PUBLIC (github.com/allansargeant/resolume-luma-keyer), tagged/released v1.0.0.
- Key design choice: replaced the SDK's fragile Xcode target-duplication workflow with a self-contained CMake MODULE build → loadable universal (arm64+x86_64) .bundle on macOS. `cmake --install` drops it into `~/Documents/Resolume Arena/Extra Effects/`.
- macOS needs no extra deps (system OpenGL framework). Windows needs GLEW via vcpkg (root vcpkg.json, triplet x64-windows-static-md so no glew32.dll runtime dep). IMPORTANT: consumer targets that include FFGL headers must link GLEW::GLEW themselves on non-Apple — the SDK links it PRIVATE, so include dirs don't propagate (this bit us in first CI run).
- Release workflow is Windows + macOS only (no Linux — Resolume doesn't run on Linux), unlike the **ci intel mac runners** (working-practice note, kept in Claude memory) pattern; macOS is a single universal job, not split arch jobs.
- Verified: builds clean locally + CI, bundle exports plugMain, universal. **Run inside Resolume on real content** as of 2026-08-02 (Allan's own report) — the earlier "not visually verified inside Resolume" is retired. An agent still can't drive the app itself; host verification comes from Allan.
- Standard AI-disclaimer applies (code plugin, like zero-eq etc — see **disclaimer scope** (working-practice note, kept in Claude memory)).
