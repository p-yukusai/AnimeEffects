#version 330

#variation IS_CLIPPEE 0

uniform vec4 uColor;
uniform int uClipperId;
uniform sampler2D uTexture;
uniform usampler2D uDestTexture;

#if IS_CLIPPEE
uniform int uClippingId;
#endif

in vec2 vTexCoord;
in vec2 vDestCoord;

out uvec2 oClip;

void main(void)
{
    vec4 color = uColor * texture(uTexture, vTexCoord);
    ivec2 destCoord = ivec2(vDestCoord);
    uvec2 destData = texelFetch(uDestTexture, destCoord, 0).xy;

#if IS_CLIPPEE
    if (uint(uClippingId) == destData.x)
    {
        oClip = uvec2(uint(uClipperId), uint(color.a * float(destData.y)));
    }
    else if (uint(uClipperId) == destData.x)
    {
        oClip = uvec2(uint(uClipperId), min(uint(255), uint(color.a * 255.0) + destData.y));
    }
    else
    {
        oClip = uvec2(0, 0);
    }
#else
    if (uint(uClipperId) == destData.x)
    {
        oClip = uvec2(uint(uClipperId), min(uint(255), uint(color.a * 255.0) + destData.y));
    }
    else
    {
        oClip = uvec2(uint(uClipperId), uint(color.a * 255.0));
    }
#endif
}
