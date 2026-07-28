# AGENTS.md — bringing an LLM up to speed on Luma Key

Orientation for an AI assistant (or a new human) picking this project up cold. `CLAUDE.md`
holds the short command reference; this file explains the model and the traps.

---

## 1. What this is

A **luminance keyer effect plugin for Resolume Arena / Avenue**, built on the official
Resolume **FFGL** SDK.

It measures each pixel's perceptual (**Rec. 709**) luminance and drives that pixel's alpha
from it, so dark — or bright — areas of a clip become transparent and let lower layers show
through. It's a quick way to key black backgrounds, handle add-blend-style content, or make
shadow/highlight mattes without a dedicated key colour.

C++/GLSL, CMake. Public repo. **Released v1.0.0.** Small: 11 tracked files.

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
