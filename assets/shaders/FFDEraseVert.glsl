#version 330

// uBrushHardness, uBrushAmount and brushFalloff() are prepended by
// ffd::TaskResource - the same function the deformer uses, so an erase has the
// shape its paint had.

in vec3 inPosition;
in vec3 inWorldPosition;
in vec3 inOriginPosition;

out vec3 outPosition;
out float outPower;

uniform vec2 uBrushCenter;
uniform float uBrushRadius;
uniform float uBrushPressure;

void main(void)
{
    float distance = length(inWorldPosition.xy - uBrushCenter);
    float weight = brushFalloff(distance, uBrushRadius, uBrushHardness) * uBrushAmount;
    float linearRate = min(distance / uBrushRadius, 1.0);

    float power = uBrushPressure * weight;
    outPosition = inOriginPosition + (1.0 - power) * (inPosition - inOriginPosition);
    outPower = weight;
}
