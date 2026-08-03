//! The After Effects / Premiere Pro build of the Luma Key effect.
//!
//! Same key as the FFGL shader in ../source/LumaKey.cpp and the OpenFX build
//! in ../source/ofx/LumaKeyOFX.cpp: Rec. 709 luminance drives the alpha
//! through a soft edge centred on Threshold, optionally inverted. This is the
//! third home of those ten lines of maths — GLSL, C++, and here — and all
//! three carry this note: when editing one, edit them all.
//!
//! Built on the `after-effects` crate rather than the Adobe C++ SDK, which
//! keeps the repo self-contained: the crate ships pre-generated bindings, an
//! AE plugin links against nothing (the host hands in function pointers), and
//! no Adobe SDK download is needed to build.
//!
//! One host quirk worth its comment: Premiere hands the legacy render path
//! BGRA where After Effects hands ARGB — the struct fields are just labelled
//! wrong in one of them. Luminance weights are the one place this matters
//! (red and blue differ by a factor of three), so the weights are swapped
//! when the host is Premiere.

use after_effects as ae;

#[derive(Eq, PartialEq, Hash, Clone, Copy, Debug)]
enum Params {
    Threshold,
    Softness,
    Invert,
}

#[derive(Default)]
struct Plugin {}

ae::define_effect!(Plugin, (), Params);

/// The key itself, straight alpha in and out. Mirrors the fragment shader:
/// smoothstep edge of width Softness centred on Threshold, flipped by Invert.
fn key_alpha(luma: f32, threshold: f32, softness: f32, invert: bool) -> f32 {
    let half_edge = softness.max(0.0001) * 0.5;
    let lo = threshold - half_edge;
    let hi = threshold + half_edge;

    let t = ((luma - lo) / (hi - lo)).clamp(0.0, 1.0);
    let k = t * t * (3.0 - 2.0 * t); // smoothstep

    if invert {
        1.0 - k
    } else {
        k
    }
}

impl AdobePluginGlobal for Plugin {
    fn can_load(_host_name: &str, _host_version: &str) -> bool {
        true
    }

    fn params_setup(
        &self,
        params: &mut ae::Parameters<Params>,
        _in_data: InData,
        _out_data: OutData,
    ) -> Result<(), Error> {
        params.add(
            Params::Threshold,
            "Threshold",
            ae::FloatSliderDef::setup(|f| {
                f.set_valid_min(0.0);
                f.set_slider_min(0.0);
                f.set_valid_max(1.0);
                f.set_slider_max(1.0);
                f.set_value(0.15);
                f.set_default(0.15);
                f.set_precision(3);
            }),
        )?;
        params.add(
            Params::Softness,
            "Softness",
            ae::FloatSliderDef::setup(|f| {
                f.set_valid_min(0.0);
                f.set_slider_min(0.0);
                f.set_valid_max(1.0);
                f.set_slider_max(1.0);
                f.set_value(0.10);
                f.set_default(0.10);
                f.set_precision(3);
            }),
        )?;
        params.add(
            Params::Invert,
            "Invert",
            ae::CheckBoxDef::setup(|c| {
                c.set_default(false);
                c.set_value(false);
            }),
        )?;
        Ok(())
    }

    fn handle_command(
        &mut self,
        cmd: ae::Command,
        in_data: ae::InData,
        mut out_data: ae::OutData,
        params: &mut ae::Parameters<Params>,
    ) -> Result<(), ae::Error> {
        match cmd {
            ae::Command::About => {
                out_data.set_return_msg(concat!(
                    "Luma Key v",
                    env!("CARGO_PKG_VERSION"),
                    "\rKeys out pixels by Rec. 709 luminance, with an adjustable ",
                    "threshold and a soft edge.\rMIT — stoatworks-labs.com"
                ));
            }
            ae::Command::Render {
                in_layer,
                mut out_layer,
            } => {
                let threshold = params.get(Params::Threshold)?.as_float_slider()?.value() as f32;
                let softness = params.get(Params::Softness)?.as_float_slider()?.value() as f32;
                let invert = params.get(Params::Invert)?.as_checkbox()?.value();

                // Premiere's legacy path is BGRA in ARGB clothing; only the
                // luminance weights care.
                let premiere = &in_data.application_id() == b"PrMr";
                let (wr, wb) = if premiere {
                    (0.0722f32, 0.2126f32)
                } else {
                    (0.2126f32, 0.0722f32)
                };

                let extent_hint = in_data.extent_hint();
                let out_extent_hint = out_layer.extent_hint();
                if extent_hint != out_extent_hint {
                    out_layer.fill(None, Some(out_extent_hint))?;
                }

                in_layer.iterate_with(
                    &mut out_layer,
                    0,
                    extent_hint.height(),
                    Some(extent_hint),
                    |_x: i32,
                     _y: i32,
                     pixel: ae::GenericPixel,
                     out_pixel: ae::GenericPixelMut|
                     -> Result<(), Error> {
                        // Work in straight 0..1 whatever the depth. AE hands
                        // the plugin straight (unmatted) colour, so unlike the
                        // FFGL build there is nothing to unpremultiply.
                        let (r, g, b, a, max) = match pixel {
                            ae::GenericPixel::Pixel8(p) => (
                                p.red as f32,
                                p.green as f32,
                                p.blue as f32,
                                p.alpha as f32,
                                ae::MAX_CHANNEL8 as f32,
                            ),
                            ae::GenericPixel::Pixel16(p) => (
                                p.red as f32,
                                p.green as f32,
                                p.blue as f32,
                                p.alpha as f32,
                                ae::MAX_CHANNEL16 as f32,
                            ),
                            _ => return Err(Error::BadCallbackParameter),
                        };

                        let luma = (wr * r + 0.7152 * g + wb * b) / max;
                        let out_a = (a / max) * key_alpha(luma, threshold, softness, invert);
                        let new_alpha = (out_a * max).round();

                        match out_pixel {
                            ae::GenericPixelMut::Pixel8(out) => {
                                out.red = r as _;
                                out.green = g as _;
                                out.blue = b as _;
                                out.alpha = new_alpha as _;
                            }
                            ae::GenericPixelMut::Pixel16(out) => {
                                out.red = r as _;
                                out.green = g as _;
                                out.blue = b as _;
                                out.alpha = new_alpha as _;
                            }
                            _ => return Err(Error::BadCallbackParameter),
                        }
                        Ok(())
                    },
                )?;
            }
            _ => {}
        }
        Ok(())
    }
}
