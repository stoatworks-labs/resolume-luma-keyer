#include "LumaKey.h"
using namespace ffglex;

enum ParamType : FFUInt32
{
	PT_THRESHOLD,
	PT_SOFTNESS,
	PT_INVERT
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

	FFGLLog::LogToHost( "Created Luma Key effect" );
}

FFResult LumaKey::InitGL( const FFGLViewportStruct* vp )
{
	if( !shader.Compile( _vertexShaderCode, _fragmentShaderCode ) )
	{
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}

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
