# Luma Key — Resolume FFGL plugin

A simple luminance keyer effect for [Resolume](https://resolume.com) Arena / Avenue,
built on the official [Resolume FFGL SDK](https://github.com/resolume/ffgl).

It measures the perceptual (Rec. 709) luminance of each pixel and drives that
pixel's alpha from it, so dark (or bright) areas of an incoming clip become
transparent and let lower layers show through — a quick way to key black
backgrounds, add-blend-style content, or shadow/highlight mattes without a
dedicated key colour.

![Before/after: colourful clip on a black background, and the same clip with the black keyed to transparency (soft edge) over a checkerboard](docs/demo-before-after.png)

*Rendered with the plugin's exact shader math (Rec. 709 luma → `smoothstep` key)
at the default Threshold 0.15 / Softness 0.10 — not a Resolume screen capture.*

## Parameters

| Parameter   | Type   | Default | Description |
|-------------|--------|---------|-------------|
| **Threshold** | 0–1 slider | 0.15 | Luma level at the centre of the key edge. Pixels darker than this are keyed out. |
| **Softness**  | 0–1 slider | 0.10 | Width of the soft edge around the threshold. `0` = a hard cut, higher = a gradual falloff. |
| **Invert**    | toggle | Off | Flip the key so that **bright** pixels are removed instead of dark ones. |

The effect works on straight (unpremultiplied) colour internally and outputs
premultiplied colour clamped to the LDR range, matching Resolume's pipeline, so
it composites cleanly with the layers beneath it.

## Requirements

- macOS with Xcode command-line tools (`clang`, `xcodebuild`)
- [CMake](https://cmake.org) 3.15+
- Resolume Arena/Avenue 7.3.1 or newer (this SDK's master branch)

## Build

The FFGL SDK is vendored as a git submodule, so clone recursively:

```sh
git clone --recursive <repo-url> resolume-luma-keyer
cd resolume-luma-keyer
# or, if you already cloned without --recursive:
git submodule update --init --recursive

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces a **universal** (arm64 + x86_64) `build/LumaKey.bundle`, which
loads in both native Apple-Silicon and Intel builds of Resolume.

For a faster single-arch development build:

```sh
cmake -S . -B build -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Install

```sh
cmake --install build
```

By default this copies the bundle to:

```
~/Documents/Resolume Arena/Extra Effects/
```

Point it elsewhere (e.g. Avenue, or a shared location) with:

```sh
cmake --install build --prefix "$HOME/Documents/Resolume Avenue/Extra Effects"
```

Restart Resolume, then find **Luma Key** under the **Effects** panel (search
"Luma"). Drag it onto a clip or layer.

## Project layout

```
CMakeLists.txt          Top-level build: builds the SDK + this MODULE bundle
cmake/Info.plist.in     Bundle Info.plist template (CFBundlePackageType = BNDL)
source/LumaKey.h        Plugin class declaration
source/LumaKey.cpp      Plugin registration, GLSL shader, parameter handling
external/ffgl/          Resolume FFGL SDK (git submodule)
```

## How it works

`source/LumaKey.cpp` registers the plugin as an `FF_EFFECT` (unique ID `LK01`)
and carries a small GLSL fragment shader. Per pixel it:

1. Un-premultiplies the input colour.
2. Computes luminance `dot(rgb, vec3(0.2126, 0.7152, 0.0722))`.
3. Builds a soft key value with `smoothstep(Threshold ± Softness/2, luma)`.
4. Optionally inverts it, multiplies the existing alpha by the key, and
   re-premultiplies.

To change the plugin's name or ID, edit the `CFFGLPluginInfo PluginInfo(...)`
block near the top of `LumaKey.cpp` (the ID must be a unique 4-character string).

## Licence

The plugin source here is provided as-is. The bundled FFGL SDK under
`external/ffgl/` retains its own Resolume licence — see `external/ffgl/LICENSE.md`.
