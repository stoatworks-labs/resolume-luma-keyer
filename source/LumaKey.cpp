#include "LumaKey.h"

#include "Diag.h"
using namespace ffglex;

enum ParamType : FFUInt32
{
	PT_THRESHOLD,
	PT_SOFTNESS,
	PT_INVERT,

	//About. FFGL has no window, so the name, the version and the links are
	//parameters the host draws. See StoatworksAboutParams.h.
	PT_ABOUT_FIRST,
	PT_COUNT = PT_ABOUT_FIRST + stoatworks::about::kParamCount
};

static CFFGLPluginInfo PluginInfo(
	PluginFactory< LumaKey >,        // Create method
	"LK01",                          // Plugin unique ID of maximum length 4.
	"Luma Key",                      // Plugin name
	2,                               // API major version number
	1,                               // API minor version number
	1,                               // Plugin major version number
	0,                               // Plugin minor version number
	FF_EFFECT,                       // Plugin type
	"Keys out pixels by luminance",  // Plugin description
	"Luma Key FFGL effect"           // About
);

static const char _vertexShaderCode[] = R"(#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
)";

static const char _fragmentShaderCode[] = R"(#version 410 core
uniform sampler2D InputTexture;
uniform float Threshold;
uniform float Softness;
uniform float Invert;

in vec2 uv;

out vec4 fragColor;

void main()
{
	vec4 color = texture( InputTexture, uv );

	//The InputTexture holds premultiplied colors, so unpremultiply before
	//measuring the actual pixel luminance.
	if( color.a > 0.0 )
		color.rgb /= color.a;

	//Rec. 709 perceptual luminance weights.
	float luma = dot( color.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );

	//Build a soft key edge centred on Threshold. keyAlpha goes 0 -> 1 as luma
	//crosses the edge, so darks below Threshold are keyed out.
	float halfEdge = max( Softness, 0.0001 ) * 0.5;
	float keyAlpha = smoothstep( Threshold - halfEdge, Threshold + halfEdge, luma );

	//Invert flips the key so that brights are removed instead of darks.
	keyAlpha = mix( keyAlpha, 1.0 - keyAlpha, Invert );

	color.a *= keyAlpha;

	//Re-premultiply and keep the result inside the LDR range the engine expects.
	color.rgb = clamp( color.rgb * color.a, vec3( 0.0 ), vec3( color.a ) );
	fragColor = color;
}
)";

LumaKey::LumaKey() :
	threshold( 0.15f ), softness( 0.10f ), invert( 0.0f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//SetParamInfof reads each default from GetFloatParameter(), so the member
	//values initialised above become the parameter defaults in the host.
	SetParamInfof( PT_THRESHOLD, "Threshold", FF_TYPE_STANDARD );
	SetParamInfof( PT_SOFTNESS, "Softness", FF_TYPE_STANDARD );
	SetParamInfof( PT_INVERT, "Invert", FF_TYPE_BOOLEAN );

	// The About block. Inline rather than through a helper: SetParamInfo is
	// protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_FIRST, "About", FF_TYPE_TEXT, "" );
	{
		FFUInt32 aboutId = PT_ABOUT_FIRST + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}

	FFGLLog::LogToHost( "Created Luma Key effect" );

	lumakey::diag::init();
}

namespace
{
/// glGetString returns nullptr if there is no current context; feeding that to
/// std::string is undefined behaviour, and a logging call must never be the
/// thing that crashes the host.
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast<const char*>( value ) : "unknown";
}
} // namespace

FFResult LumaKey::InitGL( const FFGLViewportStruct* vp )
{
	// The GL strings first, and unconditionally: when a shader will not compile
	// it is almost always the driver or the GL version, and knowing which
	// machine reported what is the whole diagnosis.
	lumakey::diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR )
						 + " renderer=" + glStringOrUnknown( GL_RENDERER )
						 + " version=" + glStringOrUnknown( GL_VERSION ) );

	if( !shader.Compile( _vertexShaderCode, _fragmentShaderCode ) )
	{
		// Returning FF_FAIL here is invisible to the operator: the effect
		// simply does nothing in Resolume, with no message anywhere. This line
		// is the only record that it was the shader.
		lumakey::diag::error( "shader failed to compile - the effect will do nothing" );
		FFGLLog::LogToHost( "Luma Key: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		lumakey::diag::error( "quad geometry failed to initialise" );
		FFGLLog::LogToHost( "Luma Key: quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	lumakey::diag::info( "initialised" );

	//Use base-class init as success result so that it retains the viewport.
	return CFFGLPlugin::InitGL( vp );
}

FFResult LumaKey::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 )
		return FF_FAIL;

	if( pGL->inputTextures[ 0 ] == NULL )
		return FF_FAIL;

	//FFGL requires us to leave the context in a default state on return, so use these scoped bindings.
	ScopedShaderBinding shaderBinding( shader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( pGL->inputTextures[ 0 ]->Handle );

	shader.Set( "InputTexture", 0 );

	//The input texture's dimensions can change each frame, so adopt its maxUV
	//through a uniform rather than rebuilding the vertex buffer.
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( *pGL->inputTextures[ 0 ] );
	shader.Set( "MaxUV", maxCoords.s, maxCoords.t );

	shader.Set( "Threshold", threshold );
	shader.Set( "Softness", softness );
	shader.Set( "Invert", invert );

	quad.Draw();

	return FF_SUCCESS;
}

FFResult LumaKey::DeInitGL()
{
	shader.FreeGLResources();
	quad.Release();

	return FF_SUCCESS;
}

FFResult LumaKey::SetFloatParameter( unsigned int dwIndex, float value )
{
	// An About button is a press, not a value to keep: it opens a browser and
	// nothing about the effect changes.
	if( dwIndex >= PT_ABOUT_FIRST )
		return stoatworks::about::handleParam( dwIndex - PT_ABOUT_FIRST, value ) ? FF_SUCCESS : FF_FAIL;

	switch( dwIndex )
	{
	case PT_THRESHOLD:
		threshold = value;
		break;
	case PT_SOFTNESS:
		softness = value;
		break;
	case PT_INVERT:
		invert = value;
		break;

	default:
		return FF_FAIL;
	}

	return FF_SUCCESS;
}

float LumaKey::GetFloatParameter( unsigned int index )
{
	switch( index )
	{
	case PT_THRESHOLD:
		return threshold;
	case PT_SOFTNESS:
		return softness;
	case PT_INVERT:
		return invert;
	}

	return 0.0f;
}

char* LumaKey::GetTextParameter( unsigned int index )
{
	// The host is handed a bare pointer, so the string is kept as a member
	// rather than built on the stack here.
	if( index == PT_ABOUT_FIRST )
	{
		aboutText = stoatworks::about::textParam( 0 );
		return const_cast< char* >( aboutText.c_str() );
	}

	return CFFGLPlugin::GetTextParameter( index );
}
