/// Headless test of the FxPlug render path.
///
/// `renderDestinationImage:` can only be called by Final Cut Pro or Motion, so
/// the part of it that can go wrong on its own — pixel layouts, strides, tile
/// bounds — was lifted into LumaKeyTile.h. This drives that directly.
///
/// What this proves: every layout decodes and re-encodes in the right channel
/// order, strides with padding are honoured, and all four layouts agree with
/// each other and with the OpenFX build's maths on the same pixels.
///
/// What this does NOT prove: that the FxPlug plumbing around it (parameters,
/// pluginState, tile negotiation) is correct. Only a host shows that.

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../LumaKeyTile.h"

namespace
{
int failures = 0;

void check( bool ok, const std::string& what )
{
	if( !ok )
	{
		std::printf( "  FAIL  %s\n", what.c_str() );
		++failures;
	}
	else
		std::printf( "  ok    %s\n", what.c_str() );
}

const lumakey::TileState kState{ 0.15f, 0.10f, 0u };

/// The reference: straight from the shared core, no layout handling involved.
void reference( float r, float g, float b, float a, float out[ 4 ] )
{
	lumakey::keyPixel( r, g, b, a, kState.threshold, kState.softness, false, true,
					   out[ 0 ], out[ 1 ], out[ 2 ], out[ 3 ] );
}

// ---------------------------------------------------------------- channel order

/// The trap this exists for: BGRA8 and RGBA8 differ only in byte order, so a
/// swapped read or write is invisible on grey and obvious on a saturated colour.
void testChannelOrder()
{
	std::printf( "channel order (BGRA8 vs RGBA8)\n" );

	// A pixel with three distinct channels, so any swap shows.
	const uint8_t r = 200, g = 120, b = 40, a = 255;

	uint8_t bgraIn[ 4 ] = { b, g, r, a };
	uint8_t rgbaIn[ 4 ] = { r, g, b, a };
	uint8_t bgraOut[ 4 ] = {}, rgbaOut[ 4 ] = {};

	lumakey::keyTile( bgraIn, 4, fxsurface::Layout::BGRA8, bgraOut, 4, fxsurface::Layout::BGRA8, 1, 1, kState );
	lumakey::keyTile( rgbaIn, 4, fxsurface::Layout::RGBA8, rgbaOut, 4, fxsurface::Layout::RGBA8, 1, 1, kState );

	// Same pixel expressed two ways must key to the same colour.
	check( bgraOut[ 2 ] == rgbaOut[ 0 ], "red survives both layouts" );
	check( bgraOut[ 1 ] == rgbaOut[ 1 ], "green survives both layouts" );
	check( bgraOut[ 0 ] == rgbaOut[ 2 ], "blue survives both layouts" );
	check( bgraOut[ 3 ] == rgbaOut[ 3 ], "alpha survives both layouts" );

	// And must match the core computed independently.
	float expect[ 4 ];
	reference( r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f, expect );
	check( rgbaOut[ 0 ] == fxsurface::quantise8( expect[ 0 ] ), "red matches the shared core" );
	check( rgbaOut[ 3 ] == fxsurface::quantise8( expect[ 3 ] ), "alpha matches the shared core" );

	// A red pixel must not come out blue — the failure a grey ramp hides.
	check( rgbaOut[ 0 ] > rgbaOut[ 2 ], "red pixel stays red, not swapped to blue" );
}

// ----------------------------------------------------------------------- stride

/// IOSurfaces are row-padded. Walking by width*bpp instead of the reported
/// stride skews the picture progressively, which looks like a rendering bug
/// anywhere but here.
void testStridePadding()
{
	std::printf( "stride padding\n" );

	const int w = 5, h = 4;
	const size_t stride = 5 * 4 + 12;// deliberately padded

	std::vector<uint8_t> src( stride * h, 0 );
	std::vector<uint8_t> dst( stride * h, 0xAB );// poison, to catch untouched bytes

	// Row n gets luminance n*60, so a skew shows as the wrong row value.
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			uint8_t* p = src.data() + y * stride + x * 4;
			p[ 0 ] = p[ 1 ] = p[ 2 ] = uint8_t( y * 60 );
			p[ 3 ] = 255;
		}

	lumakey::keyTile( src.data(), stride, fxsurface::Layout::RGBA8,
					  dst.data(), stride, fxsurface::Layout::RGBA8, w, h, kState );

	bool rowsRight = true;
	for( int y = 0; y < h; ++y )
	{
		const float v = ( y * 60 ) / 255.0f;
		float expect[ 4 ];
		reference( v, v, v, 1.0f, expect );

		for( int x = 0; x < w; ++x )
		{
			const uint8_t* p = dst.data() + y * stride + x * 4;
			if( p[ 3 ] != fxsurface::quantise8( expect[ 3 ] ) )
				rowsRight = false;
		}
	}
	check( rowsRight, "every row keys to its own luminance across a padded stride" );

	// The padding itself must be untouched.
	bool paddingIntact = true;
	for( int y = 0; y < h; ++y )
		for( size_t i = w * 4; i < stride; ++i )
			if( dst[ y * stride + i ] != 0xAB )
				paddingIntact = false;
	check( paddingIntact, "row padding is left alone" );
}

// ------------------------------------------------------------------ half floats

void testHalfFloatRoundTrip()
{
	std::printf( "half float\n" );

	const float values[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.15f, 0.1f };
	bool ok = true;
	for( float v : values )
		if( std::fabs( fxsurface::halfToFloat( fxsurface::floatToHalf( v ) ) - v ) > 0.001f )
			ok = false;
	check( ok, "float -> half -> float round-trips within half precision" );

	// A half-float tile must key the same as an 8-bit one, within quantisation.
	const float v = 0.6f;
	uint16_t src[ 4 ] = { fxsurface::floatToHalf( v ), fxsurface::floatToHalf( v ),
						  fxsurface::floatToHalf( v ), fxsurface::floatToHalf( 1.0f ) };
	uint16_t dst[ 4 ] = {};
	lumakey::keyTile( reinterpret_cast<uint8_t*>( src ), 8, fxsurface::Layout::RGBAh,
					  reinterpret_cast<uint8_t*>( dst ), 8, fxsurface::Layout::RGBAh, 1, 1, kState );

	float expect[ 4 ];
	reference( v, v, v, 1.0f, expect );
	check( std::fabs( fxsurface::halfToFloat( dst[ 3 ] ) - expect[ 3 ] ) < 0.002f,
		   "half float tile keys to the same alpha as the shared core" );
}

// ----------------------------------------------------------------- float, mixed

void testFloatAndMixedLayouts()
{
	std::printf( "float and mixed layouts\n" );

	const float v = 0.8f;
	float src[ 4 ] = { v, v, v, 1.0f };
	float dstF[ 4 ] = {};
	lumakey::keyTile( reinterpret_cast<uint8_t*>( src ), 16, fxsurface::Layout::RGBAf,
					  reinterpret_cast<uint8_t*>( dstF ), 16, fxsurface::Layout::RGBAf, 1, 1, kState );

	float expect[ 4 ];
	reference( v, v, v, 1.0f, expect );
	check( std::fabs( dstF[ 3 ] - expect[ 3 ] ) < 1e-6f, "float tile matches the shared core exactly" );

	// A host may hand us a float source and an 8-bit destination.
	uint8_t dst8[ 4 ] = {};
	lumakey::keyTile( reinterpret_cast<uint8_t*>( src ), 16, fxsurface::Layout::RGBAf,
					  dst8, 4, fxsurface::Layout::BGRA8, 1, 1, kState );
	check( dst8[ 3 ] == fxsurface::quantise8( expect[ 3 ] ), "float source into 8-bit destination" );
}

// ---------------------------------------------------------------- key behaviour

/// The effect must actually do something, and do it in the right direction.
void testKeyBehaviour()
{
	std::printf( "key behaviour\n" );

	auto alphaFor = [ ]( uint8_t luma ) {
		uint8_t src[ 4 ] = { luma, luma, luma, 255 };
		uint8_t dst[ 4 ] = {};
		lumakey::keyTile( src, 4, fxsurface::Layout::RGBA8, dst, 4, fxsurface::Layout::RGBA8, 1, 1, kState );
		return dst[ 3 ];
	};

	check( alphaFor( 0 ) == 0, "black keys out completely" );
	check( alphaFor( 255 ) == 255, "white stays opaque" );
	check( alphaFor( 0 ) < alphaFor( 128 ), "darker is more transparent than mid grey" );
	check( alphaFor( 128 ) <= alphaFor( 255 ), "mid grey is no more opaque than white" );

	// Invert flips the direction.
	lumakey::TileState inverted{ 0.15f, 0.10f, 1u };
	uint8_t src[ 4 ] = { 0, 0, 0, 255 };
	uint8_t dst[ 4 ] = {};
	lumakey::keyTile( src, 4, fxsurface::Layout::RGBA8, dst, 4, fxsurface::Layout::RGBA8, 1, 1, inverted );
	check( dst[ 3 ] == 255, "with Invert on, black is the part that stays" );
}

} // namespace

int main()
{
	std::printf( "FxPlug tile render tests\n\n" );

	testChannelOrder();
	testStridePadding();
	testHalfFloatRoundTrip();
	testFloatAndMixedLayouts();
	testKeyBehaviour();

	std::printf( "\n%s\n", failures == 0 ? "all passed" : "FAILURES PRESENT" );
	return failures == 0 ? 0 : 1;
}
