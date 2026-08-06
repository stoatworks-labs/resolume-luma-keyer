#pragma once

/// The pixel-pushing half of the FxPlug build, with no FxPlug in it.
///
/// Kept apart from LumaKeyPlugIn.mm for one reason: `renderDestinationImage:`
/// can only be called by a host, so anything left inside it is untestable. Here
/// the loop takes bare pointers, strides and layouts, which a test can supply.
///
/// Two halves either side of this file: `FxSurface.h` knows the pixel layouts
/// and nothing about the effect; `../source/LumaKeyCore.h` is the key itself,
/// shared with the OpenFX build.

#include "FxSurface.h"

#include "LumaKeyCore.h"

namespace lumakey
{

using fxsurface::Layout;

/// Everything the key needs, snapshotted from the parameters. This is the entire
/// contents of the FxPlug pluginState blob, so it must stay a POD of fixed-width
/// fields — it crosses to the render threads as raw bytes.
struct TileState
{
	float threshold;
	float softness;
	uint32_t invert;// not bool: fixed width, no padding surprises across the blob
};

/// The whole render, over one tile. FxPlug images are premultiplied.
inline void keyTile( const uint8_t* srcBase, size_t srcStride, Layout srcLayout,
					 uint8_t* dstBase, size_t dstStride, Layout dstLayout,
					 int width, int height, const TileState& state )
{
	const size_t srcBpp = fxsurface::bytesPerPixel( srcLayout );
	const size_t dstBpp = fxsurface::bytesPerPixel( dstLayout );

	for( int y = 0; y < height; ++y )
	{
		const uint8_t* srcRow = srcBase + size_t( y ) * srcStride;
		uint8_t* dstRow       = dstBase + size_t( y ) * dstStride;

		for( int x = 0; x < width; ++x )
		{
			float in[ 4 ];
			fxsurface::readPixel( srcRow + size_t( x ) * srcBpp, srcLayout, in );

			float out[ 4 ];
			keyPixel( in[ 0 ], in[ 1 ], in[ 2 ], in[ 3 ],
					  state.threshold, state.softness, state.invert != 0, /*premultiplied*/ true,
					  out[ 0 ], out[ 1 ], out[ 2 ], out[ 3 ] );

			fxsurface::writePixel( dstRow + size_t( x ) * dstBpp, dstLayout, out );
		}
	}
}

} // namespace lumakey
