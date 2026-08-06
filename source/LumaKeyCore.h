#pragma once

/// The luma key itself, with no host in it.
///
/// The maths used to live in three places at once — the GLSL in LumaKey.cpp, the
/// OpenFX C++ in ofx/LumaKeyOFX.cpp, and the Rust in adobe/src/lib.rs — and the
/// only thing keeping them equal was a comment telling you to edit all three.
/// Adding a fourth host (FxPlug, for Final Cut Pro and Motion) made that
/// untenable, so the CPU form of the key lives here and every CPU host calls it.
///
/// The GLSL and the Rust are still separate expressions of the same thing and
/// still have to be kept in step by hand; this header at least means the two
/// C++ hosts cannot drift from each other.

#include <algorithm>

namespace lumakey
{
/// Rec. 709 perceptual luminance weights, as in the shader.
inline float luminance( float r, float g, float b )
{
	return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/// Mirrors the fragment shader: smoothstep edge of width Softness centred on
/// Threshold, flipped by Invert.
inline float keyAlpha( float luma, float threshold, float softness, bool invert )
{
	const float halfEdge = std::max( softness, 0.0001f ) * 0.5f;
	const float lo       = threshold - halfEdge;
	const float hi       = threshold + halfEdge;

	float t = ( luma - lo ) / ( hi - lo );
	t       = std::min( std::max( t, 0.0f ), 1.0f );
	// smoothstep
	float k = t * t * ( 3.0f - 2.0f * t );

	return invert ? 1.0f - k : k;
}

/// One pixel of the key, in and out as straight or premultiplied float RGBA
/// depending on `premultiplied`.
///
/// Premultiplied pixels are unpremultiplied before measuring luminance and
/// re-premultiplied after the key, exactly as the GLSL build does; straight
/// pixels keep their RGB and only the alpha is scaled.
inline void keyPixel( float r, float g, float b, float a,
					  float threshold, float softness, bool invert, bool premultiplied,
					  float& outR, float& outG, float& outB, float& outA )
{
	float straightR = r, straightG = g, straightB = b;
	if( premultiplied && a > 0.0f )
	{
		straightR = r / a;
		straightG = g / a;
		straightB = b / a;
	}

	const float luma = luminance( straightR, straightG, straightB );

	outA = a * keyAlpha( luma, threshold, softness, invert );

	outR = straightR;
	outG = straightG;
	outB = straightB;
	if( premultiplied )
	{
		// Re-premultiply and keep the result inside [0, alpha], mirroring the
		// shader's clamp.
		outR = std::min( straightR * outA, outA );
		outG = std::min( straightG * outA, outA );
		outB = std::min( straightB * outA, outA );
	}
}

} // namespace lumakey
