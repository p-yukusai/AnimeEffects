#version 330

// THE FOLDER'S MATTE: one fullscreen multiply of an isolated composite by the node's alpha field.
// A LAYER applies its matte inside its own fragment pass (LayerDrawingFrag); a folder presents a
// composite, so the same multiply is this pass - and it runs AFTER the folder's filters, which is
// what stops a blurred folder from bleeding past its own matte.
//
// THE FIELD IS SAMPLED THROUGH THE SAME RECONSTRUCTION AS A LAYER MASK (LayerSampling.glsl's
// sampleMask), on BOTH paths. This pass used to take `texture(uLayerMask, uv)` - the hardware's
// implicit-LOD, possibly anisotropic read - while a layer mask drew through the two-tap
// reconstruction; the two mattes of one engine therefore had different filters at exactly the
// edges a matte exists to define. With MASK_VIRTUAL_TEXTURE the field comes from the mask's own
// page stack (a pageable matte has no full chain at all - see MaskKey::Data::virtualTexture), so
// the folder's field pages exactly as the layer's does. The mask's chain is CLAMP_TO_EDGE, and
// the page gutter repeats the level's edge texel, so both paths agree at the field's border.

#variation LAYER_MASK 1
#variation MASK_VIRTUAL_TEXTURE 0

uniform sampler2D uTexture;    // the folder's isolated composite
uniform sampler2D uLayerMask;  // the matte's full chain (also the completeness binding when paged)
uniform vec4 uLayerMaskRect;   // content x, y, w, h
uniform mat3 uScreenToLayer;

in vec2 vDestCoord;

layout(location = 0, index = 0) out vec4 oFragColor;

#include ":/shaders/LayerSampling.glsl"

void main()
{
    vec4 color = texture(uTexture, vDestCoord / vec2(textureSize(uTexture, 0)));
    vec2 q = (uScreenToLayer * vec3(vDestCoord, 1.0)).xy;
    vec2 uv = (q - uLayerMaskRect.xy) / uLayerMaskRect.zw;
    // OUTSIDE THE FIELD NOTHING IS HIDDEN: a folder matte covers the canvas, and the composite
    // outside it is empty anyway. Zeroing there instead would darken the folder's own margins.
    float visible = 1.0;
    if (0.0 <= uv.x && uv.x <= 1.0 && 0.0 <= uv.y && uv.y <= 1.0)
        visible = sampleMask(uv);
    // the composite is PREMULTIPLIED, so hiding part of it scales colour and alpha together;
    // scaling alpha alone would brighten what survives
    oFragColor = color * visible;
}
