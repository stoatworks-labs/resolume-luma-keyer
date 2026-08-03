/**
 * Luma Key — browser demo.
 *
 * The fragment shader below is `_fragmentShaderCode` from `source/LumaKey.cpp`,
 * copied across unedited. `port()` in the kit adds the `#version 300 es` line
 * and the precision qualifiers; nothing else about it is changed, which is the
 * point — the key you get here is the key the plugin computes.
 *
 * The three parameters are the three the constructor declares, with its
 * defaults (0.15, 0.10, off) and its types.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, bindTexture } from './vendor/gl.js';

//---------------------------------------------------------------------------
// Shaders — verbatim from source/LumaKey.cpp
//---------------------------------------------------------------------------

const VERTEX = `#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv = vUV * MaxUV;
}
`;

const FRAGMENT = `#version 410 core
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
`;

//---------------------------------------------------------------------------
// The renderer: one pass, exactly as ProcessOpenGL does it.
//---------------------------------------------------------------------------

function createRenderer(gl, quad) {
  const shader = new Program(gl, VERTEX, FRAGMENT, 'lumakey');

  return {
    render({ input, params }) {
      shader.use();
      bindTexture(gl, 0, input.texture);
      shader.setSampler('InputTexture', 0);

      // The clip fills the texture here, so MaxUV is 1,1 — in the host it is
      // whatever fraction of the texture the picture actually occupies.
      shader.set('MaxUV', 1, 1);

      shader.set('Threshold', params.get('threshold'));
      shader.set('Softness', params.get('softness'));
      shader.set('Invert', params.get('invert'));

      quad.draw();
    },
  };
}

//---------------------------------------------------------------------------

mountDemo({
  name: 'Luma Key',
  pluginId: 'LK01',
  tagline:
    'A luminance keyer. It measures each pixel’s Rec. 709 luminance and drives that pixel’s alpha from it, so dark — or bright — areas go transparent and let lower layers through, with no key colour to pick.',
  repo: 'https://github.com/stoatworks-labs/resolume-luma-keyer',
  page: 'https://stoatworks-labs.com/software/resolume-luma-keyer/',

  showBackdrop: true,

  params: [
    {
      id: 'threshold',
      name: 'Threshold',
      type: 'standard',
      default: 0.15,
      display: (v) => v.toFixed(3),
      hint: 'The luminance the key edge is centred on. Below it goes transparent.',
    },
    {
      id: 'softness',
      name: 'Softness',
      type: 'standard',
      default: 0.1,
      display: (v) => v.toFixed(3),
      hint: 'Width of the smoothstep across the edge — 0 is a hard cut.',
    },
    {
      id: 'invert',
      name: 'Invert',
      type: 'boolean',
      default: 0,
      hint: 'Take out the bright pixels instead of the dark ones.',
    },
  ],

  sources: ['spot', 'scene', 'ramp', 'alpha', 'bars', 'grid'],

  presets: {
    'Hard cut': { threshold: 0.2, softness: 0.0 },
    'Soft falloff': { threshold: 0.35, softness: 0.5 },
    'Knock out highlights': { threshold: 0.6, softness: 0.15, invert: 1 },
    'Shadow matte': { threshold: 0.08, softness: 0.06 },
  },

  differences: [
    'In Resolume the transparency this produces reveals the layers underneath. Here the "Behind" control stands in for that with a checkerboard or a flat colour.',
    'The plugin has no other moving parts: one pass, three uniforms, no buffers and no clock. This is as close as any of the six demos gets to being the plugin.',
  ],

  createRenderer,
});
