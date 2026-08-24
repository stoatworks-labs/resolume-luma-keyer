# AGENTS.md — bringing an LLM up to speed on Luma Key

Orientation for an AI assistant (or a new human) picking this project up cold. `CLAUDE.md`
holds the short command reference; this file explains the model and the traps.

---

## 1. What this is

A **luminance keyer effect plugin**, born for Resolume Arena / Avenue on the official
Resolume **FFGL** SDK — and since v1.1/v1.2 also shipping as an **OpenFX** plugin (Resolve,
Vegas, Nuke, Natron; `source/ofx/`) and an **After Effects / Premiere** plugin (`adobe/`,
Rust on the `after-effects` crate — no Adobe SDK involved; see CLAUDE.md for why).

It measures each pixel's perceptual (**Rec. 709**) luminance and drives that pixel's alpha
from it, so dark — or bright — areas of a clip become transparent and let lower layers show
through. It's a quick way to key black backgrounds, handle add-blend-style content, or make
shadow/highlight mattes without a dedicated key colour.

**The key maths lives three times** — the GLSL, the OFX C++, the Rust — ten lines each,
all marked. Change one, change all three. There is no shared core on purpose: the maths is
smaller than any FFI machinery that would unify it.

C++/GLSL + Rust, CMake + cargo. Public repo.

## 2. How an FFGL plugin is shaped

**The effect itself runs as GLSL inside Resolume's render pipeline. The C++ is host glue.**

That split is the thing to internalise:
- Visual behaviour lives in the **shader**.
- Parameter declaration, plugin identity and lifecycle live in the **C++ FFGL side**.

A parameter added in C++ but not consumed by the shader does nothing; a shader change without
the matching parameter plumbing isn't reachable from Resolume's UI.

Rec. 709 luminance weighting is a deliberate choice — it's perceptual, not a naive RGB
average. Don't "simplify" it.

## 3. Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output is a CMake `MODULE`: a **universal `.bundle`** on macOS, a **`.dll`** on Windows.

## 4. The macOS trap that will get you

**The macOS build must be universal (arm64 + x86_64)** or it won't load in both Resolume
builds.

And the way that breaks is silent:

> **`CMAKE_OSX_ARCHITECTURES` must be set BEFORE `project()`.** Set it afterwards and CMake
> ignores it — you get an arm64-only binary that the build log happily calls a success.

**Always verify the actual artefact**, never the build log:

```bash
lipo -archs build/*.bundle/Contents/MacOS/*
file build/*.bundle/Contents/MacOS/*
```

An Intel Resolume silently failing to load the plugin is the symptom.

## 5. Testing

There's no automated test rig — verification is loading the plugin in Resolume and looking at
it. When changing the keying maths, check against content that can actually distinguish right
from wrong: a gradient plus a mid-grey field will reveal a threshold or curve error that a
pure black-background clip hides completely.

## 6. Conventions

- Public repo. "Commit" means commit **and** push.

## Diagnostics

`source/Diag.{h,cpp}` is the smallest member of the fleet's `diag` family: a log file only.
No crash handler (this runs inside Resolume) and no bundle command (an effect is three
sliders in someone else's inspector). What it does cover is the failure that actually
happens — `InitGL` returning `FF_FAIL` because a shader would not compile, which otherwise
looks like 'the effect does nothing' with no message anywhere. The GL vendor/renderer/version
strings are logged next to it, because that is almost always the reason.


## The browser demo

`demo/` is a static page at **resolume-luma-keyer-demo.stoatworks-labs.com**: this
plugin's own GLSL, ported to WebGL2, running on clips generated in the page with
the parameters the constructor declares. It is deployed as a Cloudflare Worker
serving `demo/` as static assets (`wrangler.toml`), with **no build step** — what
is committed is what is served.

Three things about it are not visible from the files:

- **`demo/plugin.js` carries a second copy of the shader.** The demo cannot
  include a C++ file, so the GLSL from `source/LumaKey.cpp` is duplicated there and
  *nothing enforces that they agree*. Change the shader and change both, or the
  page quietly goes on rendering the old maths.
- **`demo/vendor/` is vendored, not authored here.** The master is
  `stoatworks-backend/resolume-demo/`; fix it there and re-run its `sync.sh`.
  `sync.sh --check` reports drift. A fix applied to the copy fixes one plugin out
  of six.
- **Verify a deploy by content, never by status code.** A wrong page still
  answers 200.

```bash
cf-run npx wrangler deploy
curl -s 'https://resolume-luma-keyer-demo.stoatworks-labs.com/?cb=1' | grep -o '<title>[^<]*'
```

The page is emphatic that it is not the plugin, and lists what it does not
reproduce in a disclosure at the foot. Keep that: it is a port, so nothing on it
is evidence about the plugin, and the offline harness in this repository is
still the only thing that measures anything.

## Notes

`docs/NOTES.md` carries this repo's working notes — current status, decisions
already made, and the traps that have actually bitten. Read it before changing
anything non-obvious. Cross-cutting fleet knowledge lives in
[fleet-notes](https://github.com/stoatworks-labs/fleet-notes).
