#version 330

#variation USE_SKINNING 1
#variation USE_DUAL_QUATERNION 1

in vec4  inPosition;
in ivec4 inBoneIndex0;
in ivec4 inBoneIndex1;
in vec4  inBoneWeight0;
in vec4  inBoneWeight1;

uniform mat4 uInnerMatrix;
uniform mat4 uWorldMatrix;

#if USE_DUAL_QUATERNION == 0
uniform mat4 uBoneMatrix[32];
#else
uniform vec4 uBoneDualQuat[64];
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

#elif USE_DUAL_QUATERNION == 0
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

#if 1
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

#else
    vec4 dq0[2] = vec4[2](uBoneDualQuat[2*inBoneIndex0.x], uBoneDualQuat[2*inBoneIndex0.x+1]);
    vec4 dq1[2] = vec4[2](uBoneDualQuat[2*inBoneIndex0.y], uBoneDualQuat[2*inBoneIndex0.y+1]);
    vec4 dq2[2] = vec4[2](uBoneDualQuat[2*inBoneIndex0.z], uBoneDualQuat[2*inBoneIndex0.z+1]);
    vec4 dq3[2] = vec4[2](uBoneDualQuat[2*inBoneIndex0.w], uBoneDualQuat[2*inBoneIndex0.w+1]);
    vec4 dq4[2] = vec4[2](uBoneDualQuat[2*inBoneIndex1.x], uBoneDualQuat[2*inBoneIndex1.x+1]);
    vec4 dq5[2] = vec4[2](uBoneDualQuat[2*inBoneIndex1.y], uBoneDualQuat[2*inBoneIndex1.y+1]);
    vec4 dq6[2] = vec4[2](uBoneDualQuat[2*inBoneIndex1.z], uBoneDualQuat[2*inBoneIndex1.z+1]);
    vec4 dq7[2] = vec4[2](uBoneDualQuat[2*inBoneIndex1.w], uBoneDualQuat[2*inBoneIndex1.w+1]);

    if (dot(dq0[0], dq1[0]) < 0.0) { dq1[0] *= -1.0; dq1[1] *= -1.0; }
    if (dot(dq0[0], dq2[0]) < 0.0) { dq2[0] *= -1.0; dq2[1] *= -1.0; }
    if (dot(dq0[0], dq3[0]) < 0.0) { dq3[0] *= -1.0; dq3[1] *= -1.0; }
    if (dot(dq0[0], dq4[0]) < 0.0) { dq4[0] *= -1.0; dq4[1] *= -1.0; }
    if (dot(dq0[0], dq5[0]) < 0.0) { dq5[0] *= -1.0; dq5[1] *= -1.0; }
    if (dot(dq0[0], dq6[0]) < 0.0) { dq6[0] *= -1.0; dq6[1] *= -1.0; }
    if (dot(dq0[0], dq7[0]) < 0.0) { dq7[0] *= -1.0; dq7[1] *= -1.0; }

    vec4 br =
            inBoneWeight0.x * dq0[0];
    br +=   inBoneWeight0.y * dq1[0];
    br +=   inBoneWeight0.z * dq2[0];
    br +=   inBoneWeight0.w * dq3[0];
    br +=   inBoneWeight1.x * dq4[0];
    br +=   inBoneWeight1.y * dq5[0];
    br +=   inBoneWeight1.z * dq6[0];
    br +=   inBoneWeight1.w * dq7[0];

    vec4 bd =
            inBoneWeight0.x * dq0[1];
    bd +=   inBoneWeight0.y * dq1[1];
    bd +=   inBoneWeight0.z * dq2[1];
    bd +=   inBoneWeight0.w * dq3[1];
    bd +=   inBoneWeight1.x * dq4[1];
    bd +=   inBoneWeight1.y * dq5[1];
    bd +=   inBoneWeight1.z * dq6[1];
    bd +=   inBoneWeight1.w * dq7[1];

    return dualQuatToMatrix(br, bd);
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
