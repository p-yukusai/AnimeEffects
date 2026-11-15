#version 330

// THE RUBYLITH. A mask overlay is NOT a tint on the artwork: a tint only shows where the
// composite has colour, so a stroke across transparent pixels left the red out and a stroke
// across linework dyed the lines - "why are some of the lines red?". An overlay paints the
// hidden region as a FLAT translucent red, over whatever is there and over nothing, which is
// what Photoshop's quick mask means.
//
// SAMPLED LIKE THE MATTE. The field is read through the same reconstruction the layer's own
// matte uses (LayerSampling.glsl's sampleMask), because the two are shown on top of each other:
// the overlay says where the matte hides, so a different filter there would draw a rubylith whose
// edge does not lie on the matte's. With MASK_VIRTUAL_TEXTURE the field comes from the page pool
// (the mask tool paints a pageable matte without ever building its full chain).

#variation LAYER_MASK 1
#variation MASK_VIRTUAL_TEXTURE 0

uniform sampler2D uTexture;
uniform sampler2D uLayerMask;
uniform vec4 uLayerMaskRect;   // content x, y, w, h
uniform vec4 uOverlayColor;    // rgb + alpha
uniform mat3 uScreenToLayer;

in vec2 vDestCoord;

layout(location = 0, index = 0) out vec4 oFragColor;

#include ":/shaders/LayerSampling.glsl"

void main()
{
    vec4 color = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));
    vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;
    vec2 uv = (q - uLayerMaskRect.xy) / uLayerMaskRect.zw;
    float hidden = 0.0;
    if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0)
        hidden = 1.0 - sampleMask(uv);
    float k = hidden * uOverlayColor.a;
    // src-over with a premultiplied overlay: the composite is premultiplied too
    oFragColor = color * (1.0 - k) + vec4(uOverlayColor.rgb * k, k);
}
