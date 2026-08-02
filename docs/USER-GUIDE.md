# Resolume Luma Keyer user guide

A **luminance keyer** for [Resolume](https://resolume.com) Arena and Avenue, as an FFGL effect.

It measures the perceptual (Rec. 709) luminance of each pixel and drives that pixel's alpha from
it — so dark areas of a clip become transparent and let lower layers show through. It is the
quick way to key a black background, tame add-blend-style content, or pull a shadow or highlight
matte, without needing a dedicated key colour.

> **Status:** released at v1.0.1, and **run inside Resolume on real content**. Needs Resolume
> Arena or Avenue **7.3.1 or newer**. The before/after image below is rendered with the plugin's
> exact shader maths rather than captured from Resolume.

---

## Installing

Drop the plugin into Resolume's plugin folder and restart it:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout under its own folder name. It appears in the effects list as
**Luma Key**.

The builds are unsigned, so macOS and Windows each warn once on first use — see
[UNSIGNED.md](UNSIGNED.md) for the one-time fix. On macOS, note that **approving the outer bundle
does not clear quarantine on what is nested inside it**:

```bash
xattr -dr com.apple.quarantine ~/Documents/Resolume\ Arena/Extra\ Effects/LumaKey.bundle
```

---

## The two controls

![Before and after: a colourful clip on a black background, and the same clip with the black keyed to transparency over a checkerboard, showing the soft edge.](demo-before-after.png)

*Threshold 0.15, Softness 0.10 — the defaults.*

| Parameter | Default | What it does |
|---|---|---|
| **Threshold** | 0.15 | The luma level at the **centre** of the key edge. Pixels darker than this are keyed out. |
| **Softness** | 0.10 | The **width** of the soft edge around that threshold. `0` is a hard cut; higher is a gradual falloff. |
| **Invert** | Off | Flips the key so **bright** pixels are removed instead of dark ones. |

The thing worth internalising: **Threshold is the middle of the ramp, not its start.** Raising
Softness widens the transition *around* the threshold in both directions, so a high Softness with
a low Threshold will start eating into midtones. If a key is biting too far, lower Softness before
you touch Threshold.

---

## Getting a clean key

1. **Set Softness to 0 first.** A hard cut shows you exactly where the threshold is landing;
   softness only obscures that while you are still deciding.
2. **Raise Threshold until the background is gone** and no further. Watch the darkest parts of the
   subject you want to *keep* — they go next.
3. **Then add Softness**, just enough to lose the stair-stepping on the edge.

For a highlight matte, turn **Invert** on and repeat: the same procedure, working down from white.

---

## How it composites

The effect works on **straight (unpremultiplied) colour internally** and outputs **premultiplied
colour clamped to the LDR range**, matching Resolume's own pipeline — so it composites cleanly
with the layers beneath it rather than leaving dark or bright fringes on the soft edge.

This is why it behaves properly on the *soft* part of the key and not just the hard part, which
is the usual failure of a naive luma key.

---

## What it is not

- **Not a chroma keyer.** There is no key colour; it only looks at luminance. A green screen
  needs a chroma keyer.
- **Not a spill suppressor.** It changes alpha, not colour.
- **Not a garbage matte.** Every pixel is judged on its own luminance, with no spatial awareness —
  a bright object against a black background keys well, and a bright object against a bright
  background does not key at all.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| **Plugin doesn't appear in Resolume** | Wrong folder, or Resolume older than 7.3.1. On macOS, check quarantine on the bundle itself. |
| **Key eats into the subject** | Threshold is the centre of the ramp — lower Softness before lowering Threshold. |
| **Edges are stair-stepped** | Softness is at or near 0. |
| **Dark fringe on the soft edge** | Shouldn't happen — the output is premultiplied to match Resolume. Worth reporting. |
| **Nothing keys at all** | The background may not be dark. Try **Invert**, or check the clip actually has the luma range you think. |
| **Keyed the wrong end of the range** | That is what **Invert** is for. |

---

## See also

- [README](../README.md) — parameters, build instructions and downloads
- [UNSIGNED.md](UNSIGNED.md) — Gatekeeper and SmartScreen
