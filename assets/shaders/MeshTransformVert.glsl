#version 330

#variation USE_SKINNING 1
#variation USE_DUAL_QUATERNION 1
#variation INFLUENCE_SLOTS 8
// The skeleton cap, also core::PosePalette::kMaxCount: the influence map stores
// a bone index as one uint8, so 256 is the index ceiling. The palette lives in
// a uniform BLOCK, not the default uniform block: two vec4 per bone makes 128
// bones the whole guaranteed 1024 vertex-uniform components, while a block's
// guaranteed 16384 bytes hold all 256 bones (8192 bytes). The CPU's
// MeshTransformerResource sets MAX_BONES from the C++ constant; the literal is
// only the default.
#variation MAX_BONES 256

in vec4 inPosition;

#if INFLUENCE_SLOTS == 8
in ivec4 inBoneIndex0;
in ivec4 inBoneIndex1;
in vec4  inBoneWeight0;
in vec4  inBoneWeight1;
#else
// A sparse map stores only the slots it uses: one scalar index and (above one
// influence) one scalar weight per slot. The unused inputs are optimized out.
in int inBoneIndex0;
in int inBoneIndex1;
in int inBoneIndex2;
in int inBoneIndex3;
in int inBoneIndex4;
in int inBoneIndex5;
in int inBoneIndex6;
in float inBoneWeight0;
in float inBoneWeight1;
in float inBoneWeight2;
in float inBoneWeight3;
in float inBoneWeight4;
in float inBoneWeight5;
in float inBoneWeight6;
#endif

uniform mat4 uInnerMatrix;
uniform mat4 uWorldMatrix;

#if USE_DUAL_QUATERNION == 0
// The linear-blend alternative, kept declared but never selected:
// MeshTransformer::callGL pins the dual-quaternion path. It stays at 32
// matrices because 64 mat4 is the whole guaranteed 1024-component vertex
// uniform budget by itself; it is not on the block path.
uniform mat4 uBoneMatrix[32];
#else
layout(std140) uniform BonePalette {
    // indexed by the per-vertex bone index; the CPU uploads the used prefix and
    // identity-pads a palette shorter than the map's bone count
    vec4 uBoneDualQuat[MAX_BONES * 2];
};
#endif

out vec3 outPosition;
out vec3 outXArrow;
out vec3 outYArrow;

#if USE_DUAL_QUATERNION == 1
mat4 dualQuatToMatrix(vec4 qn, vec4 qd)
{
    mat4 mtx = mat4(0.0);
    float sqLen = dot(qn, qn);
    float w = qn.x, x = qn.y, y = qn.z, z = qn.w;
    float t0 = qd.x, t1 = qd.y, t2 = qd.z, t3 = qd.w;

    mtx[0][0] = w*w + x*x - y*y - z*z;
    mtx[1][0] = 2.0*x*y - 2.0*w*z;
    mtx[2][0] = 2.0*x*z + 2.0*w*y;

    mtx[0][1] = 2.0*x*y + 2.0*w*z;
    mtx[1][1] = w*w + y*y - x*x - z*z;
    mtx[2][1] = 2.0*y*z - 2.0*w*x;

    mtx[0][2] = 2.0*x*z - 2.0*w*y;
    mtx[1][2] = 2.0*y*z + 2.0*w*x;
    mtx[2][2] = w*w + z*z - x*x - y*y;

    mtx[3][0] = -2.0*t0*x + 2.0*w*t1 - 2.0*t2*z + 2.0*y*t3;
    mtx[3][1] = -2.0*t0*y + 2.0*t1*z - 2.0*x*t3 + 2.0*w*t2;
    mtx[3][2] = -2.0*t0*z + 2.0*x*t2 + 2.0*w*t3 - 2.0*t1*y;

    mtx[3][3] = sqLen;

    return mtx / sqLen;
}
#endif

mat4 getSkinMatrix()
{
#if USE_SKINNING == 0
    return mat4(1);

#elif INFLUENCE_SLOTS == 8

    // THE DENSE FORM (all eight slots). Kept byte-for-byte beside the sparse
    // form below: a map that needs eight influences is the one case the scalar
    // attributes cannot express within the guaranteed 16 vertex attributes.
#if USE_DUAL_QUATERNION == 0
    mat4 skinMtx = mat4(0);
    skinMtx += uBoneMatrix[inBoneIndex0.x] * inBoneWeight0.x;
    skinMtx += uBoneMatrix[inBoneIndex0.y] * inBoneWeight0.y;
    skinMtx += uBoneMatrix[inBoneIndex0.z] * inBoneWeight0.z;
    skinMtx += uBoneMatrix[inBoneIndex0.w] * inBoneWeight0.w;
    skinMtx += uBoneMatrix[inBoneIndex1.x] * inBoneWeight1.x;
    skinMtx += uBoneMatrix[inBoneIndex1.y] * inBoneWeight1.y;
    skinMtx += uBoneMatrix[inBoneIndex1.z] * inBoneWeight1.z;
    skinMtx += uBoneMatrix[inBoneIndex1.w] * inBoneWeight1.w;
    return skinMtx;

#else
    ivec4 index0 = 2 * inBoneIndex0;
    ivec4 index1 = 2 * inBoneIndex1;
    mat2x4 dq0 = mat2x4(uBoneDualQuat[index0.x], uBoneDualQuat[index0.x+1]);
    mat2x4 dq1 = mat2x4(uBoneDualQuat[index0.y], uBoneDualQuat[index0.y+1]);
    mat2x4 dq2 = mat2x4(uBoneDualQuat[index0.z], uBoneDualQuat[index0.z+1]);
    mat2x4 dq3 = mat2x4(uBoneDualQuat[index0.w], uBoneDualQuat[index0.w+1]);
    mat2x4 dq4 = mat2x4(uBoneDualQuat[index1.x], uBoneDualQuat[index1.x+1]);
    mat2x4 dq5 = mat2x4(uBoneDualQuat[index1.y], uBoneDualQuat[index1.y+1]);
    mat2x4 dq6 = mat2x4(uBoneDualQuat[index1.z], uBoneDualQuat[index1.z+1]);
    mat2x4 dq7 = mat2x4(uBoneDualQuat[index1.w], uBoneDualQuat[index1.w+1]);

    if (dot(dq0[0], dq1[0]) < 0.0) dq1 *= -1.0;
    if (dot(dq0[0], dq2[0]) < 0.0) dq2 *= -1.0;
    if (dot(dq0[0], dq3[0]) < 0.0) dq3 *= -1.0;
    if (dot(dq0[0], dq4[0]) < 0.0) dq4 *= -1.0;
    if (dot(dq0[0], dq5[0]) < 0.0) dq5 *= -1.0;
    if (dot(dq0[0], dq6[0]) < 0.0) dq6 *= -1.0;
    if (dot(dq0[0], dq7[0]) < 0.0) dq7 *= -1.0;

    mat2x4 blended =
                inBoneWeight0.x * dq0;
    blended +=  inBoneWeight0.y * dq1;
    blended +=  inBoneWeight0.z * dq2;
    blended +=  inBoneWeight0.w * dq3;
    blended +=  inBoneWeight1.x * dq4;
    blended +=  inBoneWeight1.y * dq5;
    blended +=  inBoneWeight1.z * dq6;
    blended +=  inBoneWeight1.w * dq7;
    return dualQuatToMatrix(blended[0], blended[1]);
#endif

#else

    // THE SPARSE FORM (1..7 slots). Missing slots held exactly-zero weights in
    // the dense form, so omitting their terms is exact; a one-slot map's weight
    // is exactly 1 and is not stored at all.
#if USE_DUAL_QUATERNION == 0
#if INFLUENCE_SLOTS == 1
    return uBoneMatrix[inBoneIndex0];
#else
    mat4 skinMtx = uBoneMatrix[inBoneIndex0] * inBoneWeight0;
#if INFLUENCE_SLOTS >= 2
    skinMtx += uBoneMatrix[inBoneIndex1] * inBoneWeight1;
#endif
#if INFLUENCE_SLOTS >= 3
    skinMtx += uBoneMatrix[inBoneIndex2] * inBoneWeight2;
#endif
#if INFLUENCE_SLOTS >= 4
    skinMtx += uBoneMatrix[inBoneIndex3] * inBoneWeight3;
#endif
#if INFLUENCE_SLOTS >= 5
    skinMtx += uBoneMatrix[inBoneIndex4] * inBoneWeight4;
#endif
#if INFLUENCE_SLOTS >= 6
    skinMtx += uBoneMatrix[inBoneIndex5] * inBoneWeight5;
#endif
#if INFLUENCE_SLOTS >= 7
    skinMtx += uBoneMatrix[inBoneIndex6] * inBoneWeight6;
#endif
    return skinMtx;
#endif

#else
#if INFLUENCE_SLOTS == 1
    mat2x4 dq0 = mat2x4(
        uBoneDualQuat[2*inBoneIndex0], uBoneDualQuat[2*inBoneIndex0+1]
    );
    return dualQuatToMatrix(dq0[0], dq0[1]);
#else
    mat2x4 dq0 = mat2x4(
        uBoneDualQuat[2*inBoneIndex0], uBoneDualQuat[2*inBoneIndex0+1]
    );
    mat2x4 blended = inBoneWeight0 * dq0;
#if INFLUENCE_SLOTS >= 2
    mat2x4 dq1 = mat2x4(
        uBoneDualQuat[2*inBoneIndex1], uBoneDualQuat[2*inBoneIndex1+1]
    );
    if (dot(dq0[0], dq1[0]) < 0.0) dq1 *= -1.0;
    blended += inBoneWeight1 * dq1;
#endif
#if INFLUENCE_SLOTS >= 3
    mat2x4 dq2 = mat2x4(
        uBoneDualQuat[2*inBoneIndex2], uBoneDualQuat[2*inBoneIndex2+1]
    );
    if (dot(dq0[0], dq2[0]) < 0.0) dq2 *= -1.0;
    blended += inBoneWeight2 * dq2;
#endif
#if INFLUENCE_SLOTS >= 4
    mat2x4 dq3 = mat2x4(
        uBoneDualQuat[2*inBoneIndex3], uBoneDualQuat[2*inBoneIndex3+1]
    );
    if (dot(dq0[0], dq3[0]) < 0.0) dq3 *= -1.0;
    blended += inBoneWeight3 * dq3;
#endif
#if INFLUENCE_SLOTS >= 5
    mat2x4 dq4 = mat2x4(
        uBoneDualQuat[2*inBoneIndex4], uBoneDualQuat[2*inBoneIndex4+1]
    );
    if (dot(dq0[0], dq4[0]) < 0.0) dq4 *= -1.0;
    blended += inBoneWeight4 * dq4;
#endif
#if INFLUENCE_SLOTS >= 6
    mat2x4 dq5 = mat2x4(
        uBoneDualQuat[2*inBoneIndex5], uBoneDualQuat[2*inBoneIndex5+1]
    );
    if (dot(dq0[0], dq5[0]) < 0.0) dq5 *= -1.0;
    blended += inBoneWeight5 * dq5;
#endif
#if INFLUENCE_SLOTS >= 7
    mat2x4 dq6 = mat2x4(
        uBoneDualQuat[2*inBoneIndex6], uBoneDualQuat[2*inBoneIndex6+1]
    );
    if (dot(dq0[0], dq6[0]) < 0.0) dq6 *= -1.0;
    blended += inBoneWeight6 * dq6;
#endif
    return dualQuatToMatrix(blended[0], blended[1]);
#endif
#endif

#endif
}

void main(void)
{
    mat4 transform = uWorldMatrix * getSkinMatrix() * uInnerMatrix;
    outPosition = (transform * inPosition).xyz;

    vec4 origin = transform * vec4(0);
    outXArrow = (transform * vec4(1, 0, 0, 0) - origin).xyz;
    outYArrow = (transform * vec4(0, 1, 0, 0) - origin).xyz;
}
