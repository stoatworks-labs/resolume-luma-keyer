# FxPlug 4 port — the pattern

Final Cut Pro and Motion, via Apple's FxPlug 4 API. Luma Keyer is the
pipe-cleaner; the other nine follow the pattern settled here, exactly as they did
for OpenFX.

**Status:** the Luma Key build compiles universal, signs, installs, is registered
by the system, **appears in the effects browser of both Motion and Final Cut
Pro**, and **both hosts have launched its XPC service** — so the plug-in is
instantiating and answering `addParametersWithError:` / `properties:error:` in
each of them, with nothing in the log. The pixel loop is covered by a host-free
test (`fxplug/test/tiletest.cpp`, 18 checks).

What remains unconfirmed is the **picture** — nobody has yet compared a keyed
frame out of Motion or FCP against the OpenFX reference. Don't claim the render
until that holds.

Everything below is measured on this machine unless it says otherwise.

---

## 1. Why this target is not like the others

| | FFGL | OpenFX | **FxPlug 4** |
|---|---|---|---|
| Process model | in-process | in-process | **sandboxed, out-of-process (XPC)** |
| Graphics | OpenGL | CPU (our ports) | Metal — but see §4 |
| Packaging | drop-in bundle | drop-in bundle | **container `.app` + XPC service** |
| Install | copy to a folder | copy to a folder | **copy to /Applications, then run it once** |
| Signing | optional | optional | **required** — hardened runtime |
| SDK | vendored, MIT-ish | vendored, BSD-3 | **not redistributable** |

Two consequences beyond the code:

- **Signing is not optional.** An unsigned FxPlug plug-in does not load.
  `docs/UNSIGNED.md`, which explains the Gatekeeper dance for the FFGL and OpenFX
  artefacts, does not apply to this one and must not be linked from it.
- **The SDK cannot be vendored.** `external/openfx` works because the OFX SDK is
  BSD-3. Apple's is a login-gated download installed as a sparse SDK at
  `/Library/Developer/SDKs/FxPlug.sdk`, and its licence does not permit
  redistribution. **GitHub runners will not have it**, so FxPlug artefacts cannot
  come from CI. They come from the local release harness. `BUILD_FXPLUG` is
  therefore `OFF` by default and the subdirectory hard-errors if the SDK is
  missing.

## 2. What the audit found

The ten repos with OpenFX ports:

| plugin | OFX loc | contexts | needs temporal | presets |
|---|---|---|---|---|
| resolume-luma-keyer | 319 | Filter | no | no |
| porthole | 734 | Filter | no | yes |
| asciify | 852 | Filter | no | yes |
| idler | 784 | Filter, **Generator** | no | yes |
| nesolume | 921 | Filter | no | yes |
| downpour | 1018 | Filter, **Generator** | no | yes |
| flipbook | 1090 | Filter, **Generator** | no | yes |
| orrery | 1229 | Filter, **Generator** | no | yes |
| tinsel | 1342 | Filter | **yes** | yes |
| old-cathode | 1536 | Filter | **yes** | yes |

Seven are plain filters with no history — the easy wave. tinsel and old-cathode
reach backwards in time (`-scheduleInputs:withPluginState:atTime:error:` is the
API; the OFX ports' 12-frame decay-bounded cap carries over) and go last.

## 3. The reason this is cheap

FxPlug forbids reading parameters during render — the retrieval API is invalid
there and the method is called on several threads at once. You snapshot into an
opaque `pluginState` blob in `-pluginState:atTime:quality:error:` first.

The OpenFX ports **already do this**, for their own reasons. From
`porthole/source/ofx/PortholeOFX.cpp`:

```cpp
/// Everything the warp needs, in the physical units Projection.h works in.
struct WarpSettings { ... };
```

That struct *is* the plugin state. So the separation FxPlug mandates is work
already done, and the shared CPU cores link straight in as they did for OpenFX.

Keep the state a **POD of fixed-width fields** — it crosses to the render threads
as raw bytes. `TileState` uses a `uint32_t` rather than a `bool` for exactly
that reason.

## 4. Render model — no Metal in the path

FxPlug 4 deprecated OpenGL and the documentation says Metal. That is true of the
*texture* API and misleading about what a CPU renderer has to do.

`FxImageTile` exposes `ioSurface`, and `-metalTextureForDevice:` is a convenience
wrapper over it. **A CPU renderer locks the IOSurface and addresses the pixels
directly** — no Metal device, command queue, blit or upload anywhere. That is
what `LumaKeyPlugIn.mm` does, and it is why the shared core drops in unchanged.

Hand-porting GLSL to Metal is a later per-plugin optimisation, taken only where
the CPU path is measurably too slow, and never before the picture is proven
identical to the OFX build on the same frame.

Pixel formats seen in the wild are BGRA8, RGBA8, half-float and float RGBA;
`layoutForSurface()` decodes `IOSurfaceGetPixelFormat` and declines anything else
with an error rather than rendering garbage. Images are premultiplied.

**Verification:** `ofxprobe` already renders any of these plugins to a bitmap, so
the FxPlug output has a real oracle to match — more than the Adobe port ever had.

## 5. Parameter mapping

| OFX | FxPlug 4 | note |
|---|---|---|
| `defineDoubleParam` | `addFloatSliderWithName:` | |
| `defineIntParam` | `addIntSliderWithName:` | |
| `defineBooleanParam` | `addToggleButtonWithName:` | |
| `defineChoiceParam` | `addPopupMenuWithName:` | |
| `defineRGBParam` | `addColorParameterWithName:` | |
| `defineDouble2DParam` | `addPointParameterWithName:` | |
| `defineGroupParam` | `startParameterSubGroup:` / `endParameterSubGroup` | names cannot change dynamically |
| `defineStringParam` | `addStringParameterWithName:` | |
| `defineStringParam` (file path) | **no equivalent** | see below |
| About text + buttons | `addStringParameterWithName:` + `addPushButtonWithName:selector:` | mirrors the OFX About surface |

- **Better than OFX:** `addFontMenuWithName:` is a real font picker. downpour and
  flipbook currently take a font as a typed file path.
- **Worse than OFX:** there is no file-path parameter, so downpour's "Text File"
  degrades to a typed string. Document it the way the OFX temporal truncation is
  documented — a stated limit, not a silent one. **`addPathPickerWithName:` is
  not the answer**: despite the name it picks an image *mask path*, not a file.
  It looks like the fix every time you skim the header; it isn't.
- The sandbox makes that worse still. The wrapper is sandboxed, so even a
  correctly typed path may be unreadable without a security-scoped bookmark.
  Settle this before the downpour/flipbook wave.

**Parameter IDs and UUIDs are permanent identity.** A saved project refers to a
parameter by ID and to the effect by UUID. Renumber or regenerate either and every
existing use of the effect silently detaches from its values. Generate the UUIDs
once, record them here, and never regenerate — not on a rename, not on a version
bump, and above all not when copying `Info.plist.in` to the next plugin.

| plugin | group UUID | plugin UUID |
|---|---|---|
| Stoatworks (group) | `644CD859-14B1-4916-BC95-9E9588A611C3` | — |
| Luma Key | ↑ | `9ADADFC3-3F5F-4E72-A580-9ED70F709D33` |

## 6. Packaging

```
Stoatworks Luma Key.app
  Contents/
    Info.plist                     CFBundlePackageType APPL
    MacOS/Stoatworks Luma Key      the wrapper — an empty app
    Frameworks/
      FxPlug.framework             embedded from /Library/Developer/Frameworks
      PluginManager.framework      and re-signed; Headers excluded
    PlugIns/
      LumaKey.pluginkit            the XPC service that renders
        Contents/
          Info.plist               CFBundlePackageType XPC!, PlugInKit dict,
                                   PrincipalClass FxPrincipal,
                                   ProPlugPlugInList declares the effect
          MacOS/LumaKey            main() is [FxPrincipal startServicePrincipal]
```

Generators do **not** need a second bundle. `ProPlugPlugInList` is an array, so a
generator is another entry in the same service with
`protocolNames = [FxGenerator]`. The three names — `FxFilter`, `FxGenerator`,
`FxTransition` — live in Motion's `ProAppsFxSupport.framework`.

The frameworks are embedded rather than referenced because
`/Library/Developer/Frameworks` is part of the SDK install and is absent on a
user's machine. Both are universal and already `@rpath`-based; note
PluginManager is `Versions/B`, not `A`.

Apple's Xcode template does all this with custom product types
(`com.apple.fxplug.wrapperAppTarget`, `com.apple.fxplug.xpcTarget`) that are
**not installed as xcspecs on this machine**, so `fxplug/CMakeLists.txt`
assembles the layout by hand. That also keeps the build in the same CMake the
FFGL and OpenFX products use, which is what lets `source/LumaKeyCore.h` link
straight in.

## 7. Traps found building it

- **The service bundle extension is `.pluginkit`, not `.fxplug`.** With
  `.fxplug` everything builds, signs and verifies perfectly — and the plug-in
  simply never appears in `pluginkit -m -p FxPlug`, with no error anywhere.
  Motion's own `Contents/PlugIns/InternalFiltersXPC.pluginkit` is the reference.
- **Copying the app into /Applications does not register it.** Measured: the
  service appears only after the wrapper app is launched once, or after an
  explicit `pluginkit -a`. This is why the wrapper is not an empty app — running
  it *is* the install step, and its alert says so.
- **CMake escapes quotes into compiler flags.** `-F"${PATH}"` reaches clang as
  `-F\"...\"` and it looks for a directory whose name starts with a quote. Pass
  the path unquoted.
- **CMake de-duplicates `-framework`.** Two `-framework X` pairs in
  `target_link_options` lose the second flag, leaving the linker a bare framework
  name it reports as a missing file. Use `target_link_libraries` with each
  `"-framework X"` as one quoted item.
- **A `.m` file in a C++ target inherits `-std=gnu++17`** and clang refuses it.
  The sources are `.mm` throughout.
- Sign inside-out: frameworks, then the service, then the app. `codesign` will
  not honour a bundle whose contents change after it is signed.

## 7a. Testing without a host

`renderDestinationImage:` can only be called by Motion or FCP, so anything left
inside it is untestable. The pixel work therefore lives outside it, in two files,
and `fxplug/test/tiletest.cpp` drives them directly with neither the SDK nor a
running host:

- **`fxplug/FxSurface.h`** — the pixel layouts and nothing about the effect.
  Identical in every plugin, so **copy it verbatim** to the next port. There is
  no build-time link between the copies, so a fix here has to be carried out by
  hand; keep it worth copying by keeping it plugin-agnostic.
- **`fxplug/<Name>Tile.h`** — the per-plugin loop over bare pointers, strides and
  layouts, calling the shared core.


```bash
cmake --build build-fxplug --target lumakey-tiletest && ./build-fxplug/fxplug/lumakey-tiletest
```

It covers the failures that a grey test ramp hides: BGRA8 vs RGBA8 channel order
(a swap is invisible on grey, obvious on a saturated colour), padded row strides
(walking by `width * bpp` instead of the reported stride skews the picture
progressively), half-float round-tripping, mixed source/destination layouts, and
the key's direction. It does **not** cover the FxPlug plumbing around it —
parameters, `pluginState`, tile negotiation. Only a host shows those.

## 8. Order of work

1. ~~SDK in place~~ — done, `/Library/Developer/SDKs/FxPlug.sdk`.
2. ~~Luma Key builds, signs, registers~~ — done.
3. ~~Listed in Motion and FCP; both hosts launch the service~~ — done. The
   `ProPlugPlugInList` declaration is therefore correct, which was the largest
   packaging unknown.
4. **A keyed frame out of Motion or FCP, compared against `ofxprobe`** — the
   current step. Nothing may be claimed publicly before this holds.
5. The six remaining plain filters, which should be mechanical.
6. The four generators (second `ProPlugPlugInList` entry each).
7. tinsel and old-cathode — temporal access is the one genuinely new API.
8. Public format list — website suite copy, READMEs, guides — updated once step 4
   holds, mirroring how After Effects is currently worded as rolling out.

## 9. Open gaps, stated plainly

- **No keyed frame has been compared against the reference yet.** The plug-in
  loads and instantiates in both hosts; that is not the same as proving the
  picture is right.
- **Release pipeline unsolved** for a target CI cannot build.
- Both Motion 6.3 and Final Cut Pro are installed and list the effect.
- The key maths now has four homes — GLSL, OpenFX, Adobe (Rust), FxPlug. The two
  C++ ones share `source/LumaKeyCore.h`; the GLSL and the Rust are still kept in
  step by hand.
