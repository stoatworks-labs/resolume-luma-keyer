#pragma once
#include <string>
#include <FFGLSDK.h>

// A simple luminance keyer effect for Resolume Arena / Avenue.
//
// It computes the perceptual luminance of each pixel and drives its alpha
// from that value, with an adjustable threshold and a soft edge. Below the
// threshold pixels become transparent; above it they stay opaque (or the
// reverse, when Invert is on).
class LumaKey : public CFFGLPlugin
{
public:
	LumaKey();

	//CFFGLPlugin
	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int dwIndex, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	ffglex::FFGLShader shader;  //!< Compiles and links the keyer shader program.
	ffglex::FFGLScreenQuad quad;//!< Renders the full screen quad we key on.

	float threshold;//!< Luma level at the centre of the key edge (0..1).
	float softness; //!< Width of the soft edge around the threshold (0..1).
	float invert;   //!< 0 = key out darks, 1 = key out brights.
};
