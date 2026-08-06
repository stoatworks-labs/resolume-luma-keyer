/// The FxPlug 4 build of the Luma Key effect, for Final Cut Pro and Motion.
///
/// The key itself is not here — it is in source/LumaKeyCore.h, shared with the
/// OpenFX build so the two CPU hosts cannot drift apart. What is here is the
/// shape FxPlug demands, which differs from every other host we target:
///
///  - The plug-in runs OUT OF PROCESS, in a sandboxed XPC service. There is no
///    shared address space with the host at all.
///  - Parameters may only be read in -pluginState:atTime:quality:error:. During
///    -renderDestinationImage:... the retrieval API is invalid and the method is
///    called on several threads at once. So the parameters are snapshotted into
///    a POD struct and handed back to us as an opaque NSData. This is the same
///    separation the OpenFX port already makes for its own reasons.
///  - Images arrive as IOSurfaces. FxImageTile also offers
///    -metalTextureForDevice:, but that is a convenience wrapper: a CPU
///    renderer can lock the surface and address the pixels directly, with no
///    Metal device, command queue or blit anywhere in the path. That is what we
///    do, so the shared CPU core drops straight in.

#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <FxPlug/FxPlugSDK.h>

#include <algorithm>
#include <cmath>

#include "LumaKeyTile.h"

/// Our own error codes. FxPlug reserves everything below
/// kFxError_ThirdPartyDeveloperStart for itself and has no code for "I don't
/// know this pixel format", so that one is ours.
enum {
	kLumaKeyError_UnsupportedPixelFormat = kFxError_ThirdPartyDeveloperStart + 1,
};

// Parameter IDs. These are a serialisation key: a saved project refers to a
// parameter by ID, so changing one silently detaches every existing use of the
// effect from its value. Never renumber.
enum {
	kParamID_Threshold = 1,
	kParamID_Softness  = 2,
	kParamID_Invert    = 3,
};

// The pixel work lives in LumaKeyTile.h so it can be tested without a host.
using fxsurface::Layout;
using lumakey::TileState;

namespace {

/// The one FxPlug-specific piece of the pixel path: asking an IOSurface what it
/// holds. Everything downstream of this is host-free and lives in LumaKeyTile.h.
Layout layoutForSurface( IOSurfaceRef surface )
{
	switch( IOSurfaceGetPixelFormat( surface ) )
	{
	case 'BGRA': return Layout::BGRA8;
	case 'RGBA': return Layout::RGBA8;
	case 'RGhA':
	case 'RGbA': return Layout::RGBAh;
	case 'RGfA':
	case 'RGFA': return Layout::RGBAf;
	default:     return Layout::Unsupported;
	}
}

} // namespace


@interface LumaKeyPlugIn : NSObject <FxTileableEffect>
@end

@implementation LumaKeyPlugIn
{
	// Weak, as the header instructs: the API manager outlives us and retaining
	// it would make a cycle through the host.
	__weak id<PROAPIAccessing> _apiManager;
}

- (nullable instancetype)initWithAPIManager:(id<PROAPIAccessing>)apiManager
{
	self = [super init];
	if( self != nil )
		_apiManager = apiManager;
	return self;
}

- (BOOL)addParametersWithError:(NSError**)error
{
	id<FxParameterCreationAPI_v5> params =
		[_apiManager apiForProtocol:@protocol( FxParameterCreationAPI_v5 )];
	if( params == nil )
	{
		if( error != NULL )
			*error = [NSError errorWithDomain:FxPlugErrorDomain
										 code:kFxError_APIUnavailable
									 userInfo:@{ NSLocalizedDescriptionKey :
												 @"Luma Key: no parameter creation API" }];
		return NO;
	}

	// Same three controls, same defaults and ranges as every other build.
	if( ![params addFloatSliderWithName:@"Threshold"
							parameterID:kParamID_Threshold
						   defaultValue:0.15
						   parameterMin:0.0
						   parameterMax:1.0
							  sliderMin:0.0
							  sliderMax:1.0
								  delta:0.01
						 parameterFlags:kFxParameterFlag_DEFAULT] )
		return NO;

	if( ![params addFloatSliderWithName:@"Softness"
							parameterID:kParamID_Softness
						   defaultValue:0.10
						   parameterMin:0.0
						   parameterMax:1.0
							  sliderMin:0.0
							  sliderMax:1.0
								  delta:0.01
						 parameterFlags:kFxParameterFlag_DEFAULT] )
		return NO;

	if( ![params addToggleButtonWithName:@"Invert"
							 parameterID:kParamID_Invert
							defaultValue:NO
						  parameterFlags:kFxParameterFlag_DEFAULT] )
		return NO;

	return YES;
}

- (BOOL)properties:(NSDictionary* _Nonnull* _Nullable)properties error:(NSError**)error
{
	// A pure per-pixel key: every tile renders alone, the output is the size of
	// the input, and nothing varies unless a parameter does.
	*properties = @{
		kFxPropertyKey_NeedsFullBuffer            : @NO,
		kFxPropertyKey_VariesWhenParamsAreStatic  : @NO,
		kFxPropertyKey_ChangesOutputSize          : @NO,
	};
	return YES;
}

- (BOOL)pluginState:(NSData* _Nonnull* _Nullable)pluginState
			 atTime:(CMTime)renderTime
			quality:(FxQuality)qualityLevel
			  error:(NSError**)error
{
	id<FxParameterRetrievalAPI_v6> params =
		[_apiManager apiForProtocol:@protocol( FxParameterRetrievalAPI_v6 )];
	if( params == nil )
	{
		if( error != NULL )
			*error = [NSError errorWithDomain:FxPlugErrorDomain
										 code:kFxError_APIUnavailable
									 userInfo:@{ NSLocalizedDescriptionKey :
												 @"Luma Key: no parameter retrieval API" }];
		return NO;
	}

	double threshold = 0.15, softness = 0.10;
	BOOL invert      = NO;

	if( ![params getFloatValue:&threshold fromParameter:kParamID_Threshold atTime:renderTime] ||
		![params getFloatValue:&softness  fromParameter:kParamID_Softness  atTime:renderTime] ||
		![params getBoolValue:&invert     fromParameter:kParamID_Invert    atTime:renderTime] )
	{
		if( error != NULL )
			*error = [NSError errorWithDomain:FxPlugErrorDomain
										 code:kFxError_InvalidParameter
									 userInfo:@{ NSLocalizedDescriptionKey :
												 @"Luma Key: could not read parameters" }];
		return NO;
	}

	TileState state;
	state.threshold = float( threshold );
	state.softness  = float( softness );
	state.invert    = invert ? 1u : 0u;

	// A fresh NSData every call, as the header requires — this method runs on
	// several threads at once and the host keeps whatever we hand back.
	*pluginState = [NSData dataWithBytes:&state length:sizeof( state )];
	return YES;
}

- (BOOL)destinationImageRect:(FxRect*)destinationImageRect
				sourceImages:(NSArray<FxImageTile*>*)sourceImages
			destinationImage:(FxImageTile*)destinationImage
				 pluginState:(nullable NSData*)pluginState
					  atTime:(CMTime)renderTime
					   error:(NSError**)outError
{
	// Same size as the input: a key changes alpha, never geometry.
	if( sourceImages.count > 0 )
		*destinationImageRect = sourceImages[ 0 ].imagePixelBounds;
	else
		*destinationImageRect = destinationImage.imagePixelBounds;
	return YES;
}

- (BOOL)sourceTileRect:(FxRect*)sourceTileRect
	  sourceImageIndex:(NSUInteger)sourceImageIndex
		  sourceImages:(NSArray<FxImageTile*>*)sourceImages
   destinationTileRect:(FxRect)destinationTileRect
	  destinationImage:(FxImageTile*)destinationImage
		   pluginState:(nullable NSData*)pluginState
				atTime:(CMTime)renderTime
				 error:(NSError**)outError
{
	// Every output pixel depends on exactly the input pixel beneath it, so the
	// tile we need is the tile we are asked for.
	*sourceTileRect = destinationTileRect;
	return YES;
}

- (BOOL)renderDestinationImage:(FxImageTile*)destinationImage
				  sourceImages:(NSArray<FxImageTile*>*)sourceImages
				   pluginState:(nullable NSData*)pluginState
						atTime:(CMTime)renderTime
						 error:(NSError**)outError
{
	if( pluginState == nil || pluginState.length != sizeof( TileState ) )
	{
		if( outError != NULL )
			*outError = [NSError errorWithDomain:FxPlugErrorDomain
											code:kFxError_InvalidParameter
										userInfo:@{ NSLocalizedDescriptionKey :
													@"Luma Key: bad plug-in state" }];
		return NO;
	}

	TileState state;
	[pluginState getBytes:&state length:sizeof( state )];

	if( sourceImages.count < 1 )
	{
		if( outError != NULL )
			*outError = [NSError errorWithDomain:FxPlugErrorDomain
											code:kFxError_InvalidParameter
										userInfo:@{ NSLocalizedDescriptionKey :
													@"Luma Key: no source image" }];
		return NO;
	}

	FxImageTile* source = sourceImages[ 0 ];

	IOSurfaceRef srcSurface = (__bridge IOSurfaceRef)source.ioSurface;
	IOSurfaceRef dstSurface = (__bridge IOSurfaceRef)destinationImage.ioSurface;
	if( srcSurface == NULL || dstSurface == NULL )
	{
		if( outError != NULL )
			*outError = [NSError errorWithDomain:FxPlugErrorDomain
											code:kFxError_InvalidParameter
										userInfo:@{ NSLocalizedDescriptionKey :
													@"Luma Key: image tile carried no surface" }];
		return NO;
	}

	const Layout srcLayout = layoutForSurface( srcSurface );
	const Layout dstLayout = layoutForSurface( dstSurface );
	if( srcLayout == Layout::Unsupported || dstLayout == Layout::Unsupported )
	{
		if( outError != NULL )
			*outError = [NSError errorWithDomain:FxPlugErrorDomain
											code:kLumaKeyError_UnsupportedPixelFormat
										userInfo:@{ NSLocalizedDescriptionKey :
													@"Luma Key: unsupported pixel format" }];
		return NO;
	}

	IOSurfaceLock( srcSurface, kIOSurfaceLockReadOnly, NULL );
	IOSurfaceLock( dstSurface, 0, NULL );

	// The tile bounds are in image space; the surface starts at the tile's own
	// origin, so index relative to each tile's lower-left corner.
	const FxRect dstBounds = destinationImage.tilePixelBounds;
	const FxRect srcBounds = source.tilePixelBounds;

	const int width  = int( std::min( dstBounds.right - dstBounds.left,
									  srcBounds.right - srcBounds.left ) );
	const int height = int( std::min( dstBounds.top - dstBounds.bottom,
									  srcBounds.top - srcBounds.bottom ) );

	lumakey::keyTile( static_cast<const uint8_t*>( IOSurfaceGetBaseAddress( srcSurface ) ),
					  IOSurfaceGetBytesPerRow( srcSurface ), srcLayout,
					  static_cast<uint8_t*>( IOSurfaceGetBaseAddress( dstSurface ) ),
					  IOSurfaceGetBytesPerRow( dstSurface ), dstLayout,
					  width, height, state );

	IOSurfaceUnlock( dstSurface, 0, NULL );
	IOSurfaceUnlock( srcSurface, kIOSurfaceLockReadOnly, NULL );

	return YES;
}

@end
