/// The OpenFX build of the Luma Key effect, for DaVinci Resolve, Nuke,
/// Natron, Vegas and other OFX hosts.
///
/// Same key as the FFGL shader in LumaKey.cpp: Rec. 709 luminance drives the
/// alpha through a soft edge centred on Threshold, optionally inverted. The
/// FFGL build renders it as GLSL inside Resolume's GL pipeline; here the host
/// hands us plain pixel buffers, so the identical math runs on the CPU. The
/// two implementations must key identically — when editing one, edit both.

#include <algorithm>
#include <cmath>
#include <cstring>

#include "ofxsImageEffect.h"
#include "ofxsProcessing.h"

#include "../LumaKeyCore.h"

namespace
{
constexpr const char* kPluginIdentifier = "com.stoatworks.lumakey";
constexpr const char* kPluginName       = "Luma Key";
constexpr const char* kPluginGrouping   = "Stoatworks";
constexpr const char* kPluginDescription =
	"Keys out pixels by luminance.\n\n"
	"Computes the Rec. 709 perceptual luminance of each pixel and drives its "
	"alpha from that value, with an adjustable threshold and a soft edge. "
	"Below the threshold pixels become transparent; above it they stay opaque "
	"(or the reverse, when Invert is on).\n\n"
	"https://stoatworks-labs.com";

constexpr const char* kParamThreshold = "threshold";
constexpr const char* kParamSoftness  = "softness";
constexpr const char* kParamInvert    = "invert";

class LumaKeyProcessorBase : public OFX::ImageProcessor
{
public:
	explicit LumaKeyProcessorBase( OFX::ImageEffect& effect ) :
		OFX::ImageProcessor( effect )
	{
	}

	void setSrcImg( OFX::Image* v )
	{
		srcImg = v;
	}
	void setParams( float thresholdValue, float softnessValue, bool invertValue, bool premultipliedValue )
	{
		threshold     = thresholdValue;
		softness      = softnessValue;
		invert        = invertValue;
		premultiplied = premultipliedValue;
	}

protected:
	OFX::Image* srcImg = nullptr;
	float threshold    = 0.15f;
	float softness     = 0.10f;
	bool invert        = false;
	/// Whether the clip carries premultiplied colour. Premultiplied pixels are
	/// unpremultiplied before measuring luminance and re-premultiplied after
	/// the key, exactly as the GLSL build does; straight pixels keep their RGB
	/// and only the alpha is scaled.
	bool premultiplied = false;
};

template<class PIX, int nComponents, int maxValue>
class LumaKeyProcessor : public LumaKeyProcessorBase
{
public:
	explicit LumaKeyProcessor( OFX::ImageEffect& effect ) :
		LumaKeyProcessorBase( effect )
	{
	}

	void multiThreadProcessImages( OfxRectI window ) override
	{
		for( int y = window.y1; y < window.y2; ++y )
		{
			if( _effect.abort() )
				break;

			PIX* dstPix = static_cast<PIX*>( _dstImg->getPixelAddress( window.x1, y ) );

			for( int x = window.x1; x < window.x2; ++x, dstPix += nComponents )
			{
				const PIX* srcPix = srcImg ? static_cast<const PIX*>( srcImg->getPixelAddress( x, y ) ) : nullptr;
				if( !srcPix )
				{
					// Outside the source: transparent black, same as sampling
					// past the FFGL texture edge.
					std::memset( dstPix, 0, sizeof( PIX ) * nComponents );
					continue;
				}

				float r = srcPix[ 0 ] / float( maxValue );
				float g = srcPix[ 1 ] / float( maxValue );
				float b = srcPix[ 2 ] / float( maxValue );
				float a = nComponents == 4 ? srcPix[ 3 ] / float( maxValue ) : 1.0f;

				float outR, outG, outB, outA;
				lumakey::keyPixel( r, g, b, a,
								   threshold, softness, invert, premultiplied,
								   outR, outG, outB, outA );

				dstPix[ 0 ] = quantise( outR );
				dstPix[ 1 ] = quantise( outG );
				dstPix[ 2 ] = quantise( outB );
				if( nComponents == 4 )
					dstPix[ 3 ] = quantise( outA );
			}
		}
	}

private:
	static PIX quantise( float v )
	{
		if( maxValue == 1 )
			return PIX( v );// float pipeline: pass through, no clamp needed beyond the key's own

		v = std::min( std::max( v, 0.0f ), 1.0f );
		return PIX( v * maxValue + 0.5f );
	}
};

class LumaKeyPlugin : public OFX::ImageEffect
{
public:
	explicit LumaKeyPlugin( OfxImageEffectHandle handle ) :
		OFX::ImageEffect( handle )
	{
		dstClip   = fetchClip( kOfxImageEffectOutputClipName );
		srcClip   = fetchClip( kOfxImageEffectSimpleSourceClipName );
		threshold = fetchDoubleParam( kParamThreshold );
		softness  = fetchDoubleParam( kParamSoftness );
		invert    = fetchBooleanParam( kParamInvert );
	}

	void render( const OFX::RenderArguments& args ) override
	{
		std::unique_ptr<OFX::Image> dst( dstClip->fetchImage( args.time ) );
		std::unique_ptr<OFX::Image> src( srcClip->fetchImage( args.time ) );

		const OFX::BitDepthEnum depth          = dst->getPixelDepth();
		const OFX::PixelComponentEnum comps    = dst->getPixelComponents();
		const bool premultiplied               = srcClip->getPreMultiplication() == OFX::eImagePreMultiplied;

		if( comps != OFX::ePixelComponentRGBA && comps != OFX::ePixelComponentRGB )
			OFX::throwSuiteStatusException( kOfxStatErrUnsupported );

		switch( depth )
		{
		case OFX::eBitDepthUByte:
			comps == OFX::ePixelComponentRGBA
				? run<LumaKeyProcessor<unsigned char, 4, 255>>( args, dst.get(), src.get(), premultiplied )
				: run<LumaKeyProcessor<unsigned char, 3, 255>>( args, dst.get(), src.get(), premultiplied );
			break;
		case OFX::eBitDepthUShort:
			comps == OFX::ePixelComponentRGBA
				? run<LumaKeyProcessor<unsigned short, 4, 65535>>( args, dst.get(), src.get(), premultiplied )
				: run<LumaKeyProcessor<unsigned short, 3, 65535>>( args, dst.get(), src.get(), premultiplied );
			break;
		case OFX::eBitDepthFloat:
			comps == OFX::ePixelComponentRGBA
				? run<LumaKeyProcessor<float, 4, 1>>( args, dst.get(), src.get(), premultiplied )
				: run<LumaKeyProcessor<float, 3, 1>>( args, dst.get(), src.get(), premultiplied );
			break;
		default:
			OFX::throwSuiteStatusException( kOfxStatErrUnsupported );
		}
	}

	bool isIdentity( const OFX::IsIdentityArguments& args, OFX::Clip*& identityClip, double& identityTime ) override
	{
		// Softness 0 with Threshold 0 keys nothing out (smoothstep of luma >= 0
		// is 1 everywhere except exactly luma 0), but that is not a guarantee
		// worth encoding; only the trivially inert case is declared identity.
		if( threshold->getValueAtTime( args.time ) <= 0.0 && softness->getValueAtTime( args.time ) <= 0.0
			&& !invert->getValueAtTime( args.time ) )
		{
			identityClip = srcClip;
			identityTime = args.time;
			return true;
		}
		return false;
	}

private:
	template<class Processor>
	void run( const OFX::RenderArguments& args, OFX::Image* dst, OFX::Image* src, bool premultiplied )
	{
		Processor processor( *this );
		processor.setDstImg( dst );
		processor.setSrcImg( src );
		processor.setParams( float( threshold->getValueAtTime( args.time ) ),
							 float( softness->getValueAtTime( args.time ) ),
							 invert->getValueAtTime( args.time ),
							 premultiplied );
		processor.setRenderWindow( args.renderWindow );
		processor.process();
	}

	OFX::Clip* dstClip             = nullptr;
	OFX::Clip* srcClip             = nullptr;
	OFX::DoubleParam* threshold    = nullptr;
	OFX::DoubleParam* softness     = nullptr;
	OFX::BooleanParam* invert      = nullptr;
};

} // namespace

mDeclarePluginFactory( LumaKeyPluginFactory, {}, {} );

void LumaKeyPluginFactory::describe( OFX::ImageEffectDescriptor& desc )
{
	desc.setLabels( kPluginName, kPluginName, kPluginName );
	desc.setPluginGrouping( kPluginGrouping );
	desc.setPluginDescription( kPluginDescription );

	desc.addSupportedContext( OFX::eContextFilter );
	desc.addSupportedContext( OFX::eContextGeneral );

	desc.addSupportedBitDepth( OFX::eBitDepthUByte );
	desc.addSupportedBitDepth( OFX::eBitDepthUShort );
	desc.addSupportedBitDepth( OFX::eBitDepthFloat );

	// Pure per-pixel effect: any tile can render alone, any frame in any
	// order, on as many threads as the host likes.
	desc.setSupportsTiles( true );
	desc.setTemporalClipAccess( false );
	desc.setRenderThreadSafety( OFX::eRenderFullySafe );
	desc.setSupportsMultiResolution( true );
}

void LumaKeyPluginFactory::describeInContext( OFX::ImageEffectDescriptor& desc, OFX::ContextEnum )
{
	OFX::ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );
	srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
	srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
	srcClip->setSupportsTiles( true );

	OFX::ClipDescriptor* dstClip = desc.defineClip( kOfxImageEffectOutputClipName );
	dstClip->addSupportedComponent( OFX::ePixelComponentRGBA );
	dstClip->addSupportedComponent( OFX::ePixelComponentRGB );
	dstClip->setSupportsTiles( true );

	OFX::PageParamDescriptor* page = desc.definePageParam( "Controls" );

	OFX::DoubleParamDescriptor* thresholdParam = desc.defineDoubleParam( kParamThreshold );
	thresholdParam->setLabels( "Threshold", "Threshold", "Threshold" );
	thresholdParam->setHint( "Luma level at the centre of the key edge." );
	thresholdParam->setRange( 0.0, 1.0 );
	thresholdParam->setDisplayRange( 0.0, 1.0 );
	thresholdParam->setDefault( 0.15 );
	page->addChild( *thresholdParam );

	OFX::DoubleParamDescriptor* softnessParam = desc.defineDoubleParam( kParamSoftness );
	softnessParam->setLabels( "Softness", "Softness", "Softness" );
	softnessParam->setHint( "Width of the soft edge around the threshold." );
	softnessParam->setRange( 0.0, 1.0 );
	softnessParam->setDisplayRange( 0.0, 1.0 );
	softnessParam->setDefault( 0.10 );
	page->addChild( *softnessParam );

	OFX::BooleanParamDescriptor* invertParam = desc.defineBooleanParam( kParamInvert );
	invertParam->setLabels( "Invert", "Invert", "Invert" );
	invertParam->setHint( "Key out brights instead of darks." );
	invertParam->setDefault( false );
	page->addChild( *invertParam );
}

OFX::ImageEffect* LumaKeyPluginFactory::createInstance( OfxImageEffectHandle handle, OFX::ContextEnum )
{
	return new LumaKeyPlugin( handle );
}

void OFX::Plugin::getPluginIDs( OFX::PluginFactoryArray& ids )
{
	// Deliberately leaked: a by-value static would register an exit-time
	// destructor inside this module, and a host that dlclose()s the bundle
	// before process exit then jumps through a dangling pointer. A plugin
	// cannot know which hosts unload, so it must not leave destructors behind.
	static LumaKeyPluginFactory* factory =
		new LumaKeyPluginFactory( kPluginIdentifier, PLUGIN_VERSION_MAJOR, PLUGIN_VERSION_MINOR );
	ids.push_back( factory );
}
