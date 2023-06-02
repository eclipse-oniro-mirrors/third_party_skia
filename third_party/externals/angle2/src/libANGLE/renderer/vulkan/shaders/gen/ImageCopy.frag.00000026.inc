// GENERATED FILE - DO NOT EDIT.
// Generated by gen_vk_internal_shaders.py.
//
// Copyright 2018 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// shaders/gen/ImageCopy.frag.00000026.inc:
//   Pre-generated shader for the ANGLE Vulkan back-end.

#pragma once
constexpr uint8_t kImageCopy_frag_00000026[] = {
    0x1f,0x8b,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0xff,0x85,0x96,0x8b,0x53,0x55,0x55,
    0x14,0xc6,0x0f,0xf7,0xc2,0xbd,0x3c,0x44,0x10,0x50,0x43,0xc5,0xf2,0x41,0x0f,0x31,
    0xc4,0x02,0x33,0x81,0x94,0x8c,0x24,0xa3,0x14,0x34,0x2b,0xa1,0x08,0x03,0x4b,0x4c,
    0x92,0x24,0x4d,0x22,0x31,0x45,0x2d,0xad,0xa4,0xf2,0xd1,0x4b,0xac,0x44,0x2b,0x11,
    0xeb,0xbf,0xe8,0x2f,0x6a,0x7a,0xcc,0x34,0xd3,0x5e,0xe7,0xfe,0x16,0xf3,0xcd,0x1d,
    0x2b,0x66,0xf6,0xdc,0xb3,0xbe,0xf5,0xed,0xb5,0xbf,0xb5,0xd6,0x3e,0xeb,0x90,0x4c,
    0xac,0x48,0x47,0x51,0x4e,0x54,0x18,0xe5,0x47,0xbf,0x44,0x99,0xbf,0x79,0x51,0x22,
    0x20,0x51,0x54,0x14,0xa5,0xe2,0xdf,0x2d,0xed,0x3b,0xda,0x6b,0x0f,0x0d,0xf7,0xd5,
    0xd6,0x37,0xd4,0x99,0x7f,0x6e,0x94,0x8c,0x79,0xe6,0x2b,0x89,0xd2,0x51,0x6e,0xf8,
    0xb5,0x75,0xa0,0x77,0xdf,0xa0,0xe1,0xc5,0x61,0xdd,0x0e,0xab,0x34,0xf0,0x0c,0x4f,
    0x5b,0x8c,0xf0,0x54,0x1c,0xc7,0xb4,0x3d,0x51,0xd4,0x16,0xe5,0x45,0xe5,0x9c,0xb7,
    0x82,0x5f,0xc7,0x72,0xc0,0xf2,0x05,0x4b,0x80,0x95,0x0a,0x96,0x04,0xab,0x10,0x2c,
    0x17,0xec,0x2e,0xc1,0xf2,0xc0,0x96,0x08,0x96,0x02,0xbb,0x47,0xb0,0x34,0xd8,0x4a,
    0xc1,0xf2,0xc1,0xee,0x17,0xac,0x00,0x6c,0xb5,0x60,0x85,0x60,0x75,0x82,0x15,0x81,
    0xd5,0x0b,0x36,0x07,0x6c,0xbd,0x60,0xc5,0x60,0x4d,0x71,0x9d,0x92,0xb3,0xf9,0x5a,
    0xcd,0x36,0x87,0xdf,0xe5,0xd4,0xc7,0xed,0x65,0x62,0x5b,0x9d,0x97,0x62,0x97,0x87,
    0x5d,0x89,0xd8,0x9f,0x8c,0x6b,0x63,0xcf,0xf3,0x03,0x27,0x45,0x9e,0x56,0xd7,0x85,
    0xc1,0x4e,0x93,0x7f,0x22,0xc6,0x73,0xe3,0x1c,0xd3,0xac,0x05,0x61,0x67,0x21,0x7c,
    0xe3,0x16,0x91,0x5b,0x2e,0xdc,0x39,0xc4,0x31,0xbc,0x11,0xbb,0x58,0x62,0xcf,0x85,
    0xef,0x67,0x97,0x11,0x2b,0x8a,0x75,0x96,0xce,0xd6,0xd9,0x35,0xd8,0x2a,0xfb,0x9f,
    0xe5,0x1c,0x3b,0xbf,0x82,0xfa,0x97,0x73,0x7e,0x45,0x7c,0x4e,0x06,0xab,0x21,0xd7,
    0x05,0xe8,0x31,0xfe,0x42,0x7c,0x69,0xf1,0x57,0x49,0x3e,0x4b,0xf1,0x97,0xc5,0xf7,
    0x28,0x11,0xd7,0xb6,0x06,0xdd,0x5e,0x77,0xe3,0xdd,0x2b,0x9a,0x3d,0x4e,0x0d,0x77,
    0xcb,0xf9,0x6b,0x39,0xd7,0xfd,0xeb,0xa8,0x8d,0xd5,0xe5,0x31,0xf6,0xfa,0xb9,0x1b,
    0x89,0x67,0x78,0x65,0x50,0xd0,0x22,0x3d,0xd1,0xbf,0x1c,0xd1,0xf0,0x38,0xcf,0x2d,
    0xe4,0x6e,0xf6,0x66,0x30,0x3f,0xb3,0x35,0xcb,0xde,0xca,0x7b,0x62,0xfb,0x9f,0x26,
    0xd7,0x14,0x9a,0x9e,0xe5,0x39,0x29,0xfc,0x4e,0x34,0xb8,0xbd,0x4b,0x72,0xb6,0x1c,
    0xbb,0xb3,0xf8,0x7b,0xc9,0xc3,0xe2,0x8d,0x50,0x47,0xcf,0xf1,0x5d,0x7c,0x23,0xc2,
    0x1f,0xe3,0x9d,0x32,0xfe,0x29,0xf8,0x7a,0xde,0x39,0x34,0xda,0xfe,0x4f,0xd9,0x5f,
    0x26,0xfe,0xcb,0xbc,0x43,0xae,0xe7,0xba,0xdc,0xe3,0x19,0xb4,0x8d,0x50,0x9f,0x19,
    0xe6,0x50,0x32,0xee,0x45,0x5e,0xcc,0xcb,0xa5,0x3e,0x86,0xfd,0x11,0x90,0x3c,0xb8,
    0xf9,0x72,0x4f,0xdc,0x5e,0x2c,0xb6,0xf5,0x6b,0x93,0xd8,0x96,0xdb,0xa8,0xd8,0x76,
    0x3f,0x2e,0x61,0x37,0xf3,0xde,0x94,0xd0,0xff,0x6d,0x01,0x9d,0xcb,0xec,0x2a,0x61,
    0x79,0x6f,0x07,0x79,0x1f,0xe7,0xe1,0x6f,0x09,0x8a,0x16,0x32,0xbf,0xe6,0x73,0x97,
    0x9b,0xe1,0x54,0x82,0x9f,0x08,0x1c,0xb3,0x17,0xb1,0xaf,0x32,0xbe,0x47,0xc9,0x58,
    0xff,0x22,0xe1,0x2f,0x21,0x27,0xf3,0x2d,0xc6,0xb6,0xf8,0x76,0xe7,0xef,0x26,0x7e,
    0x15,0x7c,0x7f,0x4f,0x0d,0x9f,0x0e,0x9c,0x65,0x32,0x1f,0xed,0x3d,0xf8,0x3d,0xc4,
    0xa8,0x46,0xf7,0x5f,0x81,0xef,0x73,0xb2,0x9a,0x3a,0xae,0x24,0xb6,0xd5,0xe1,0x3e,
    0x6a,0xb7,0x9c,0xd8,0x29,0xe6,0xa7,0xe1,0xc7,0xb0,0x1f,0x00,0xf3,0x3d,0xab,0x64,
    0x8f,0xe9,0x5d,0x05,0xe7,0xcf,0x10,0xdb,0xcf,0xa8,0x16,0xfd,0xab,0xd1,0x5f,0x23,
    0xfa,0x1f,0x04,0x77,0xfd,0xb5,0x60,0xae,0xbf,0x4e,0xf4,0x9b,0x6f,0x0d,0xf3,0xda,
    0x62,0xaf,0x11,0x2d,0x0f,0xa1,0x65,0xad,0xe8,0x7f,0x18,0xdc,0xf5,0xd7,0x83,0xf9,
    0x9e,0x06,0xd9,0x63,0xfa,0x1b,0xe0,0x98,0x7e,0x3f,0xa3,0x4e,0xf4,0x3f,0x82,0xfe,
    0x75,0xa2,0x7f,0x3d,0xb8,0xeb,0x7f,0x14,0xcc,0xf5,0x37,0x8a,0x7e,0xf3,0x6d,0x88,
    0xef,0x5e,0x26,0xf6,0x06,0xe9,0x7b,0x13,0x5a,0xec,0xde,0x65,0xee,0x63,0x06,0x6b,
    0x92,0x79,0xe2,0x77,0xa2,0x19,0x8d,0x1e,0xa7,0x91,0x38,0x36,0x63,0x9e,0x60,0xbe,
    0xf8,0x9d,0x7c,0x12,0xcd,0xad,0x72,0xd6,0x16,0x70,0xb7,0xdb,0x38,0xfb,0x38,0x77,
    0xf4,0x29,0x38,0x6d,0xc4,0xb1,0x19,0xd4,0x4e,0x9c,0xad,0x52,0xdf,0x67,0xc0,0x3b,
    0x02,0x27,0x15,0x6b,0xcf,0xec,0x8d,0x04,0xdb,0x0e,0x96,0x13,0x3f,0xa7,0xe2,0xf9,
    0xd5,0x01,0x77,0x3b,0x31,0xfc,0x8c,0x1d,0x9c,0xd1,0x29,0x67,0xec,0x04,0xef,0x09,
    0xca,0x6c,0xee,0x3e,0x47,0x8e,0x1d,0xcc,0x90,0x9d,0xd4,0x65,0x13,0x3e,0xef,0xd5,
    0xf3,0xc4,0xda,0x25,0xbd,0x7a,0x01,0xdc,0x7b,0xf5,0x22,0x98,0xf7,0xaa,0x4b,0x7a,
    0x65,0xbe,0xdd,0x61,0xf5,0x53,0xe3,0xdd,0x72,0x6f,0x5e,0x62,0xae,0x74,0x8b,0xce,
    0x97,0xc1,0x9b,0xf9,0x6e,0xf4,0xc0,0xd9,0x16,0xa6,0x92,0xe5,0xfc,0x0a,0x58,0x8f,
    0xcc,0x91,0x84,0xd4,0xa4,0x97,0x18,0xbe,0xc6,0xc3,0x59,0x86,0xef,0x61,0x6f,0xaf,
    0xc4,0x7e,0x75,0x36,0x76,0x41,0x6c,0xf7,0x81,0xed,0x61,0x56,0xe6,0xc9,0x37,0xc2,
    0x6b,0xd3,0xc7,0x9d,0xe9,0x22,0x9f,0x7e,0xa9,0xd5,0x6b,0xd4,0x6a,0xaf,0xd4,0xea,
    0x75,0x70,0xaf,0xd5,0x3e,0x30,0xaf,0xd5,0x7e,0xa9,0x95,0xf9,0x06,0xc2,0xda,0x4f,
    0xec,0x01,0xa9,0xd5,0x1b,0x77,0xa8,0xd5,0x01,0xf0,0x19,0x62,0x0f,0x82,0xb5,0xa2,
    0xd1,0xe3,0xd8,0xef,0x6f,0xa1,0xef,0xc6,0x79,0x13,0x0d,0xfd,0xf0,0x07,0xd0,0x31,
    0x24,0x3a,0x8c,0x73,0x30,0xac,0x21,0xf6,0x1f,0x14,0x1d,0x6f,0xdd,0x41,0xc7,0x21,
    0x70,0xaf,0xeb,0x70,0x56,0xcf,0xde,0x06,0x1b,0xfe,0x97,0x9e,0x1d,0x26,0x86,0xaf,
    0x33,0xf4,0xec,0x08,0x7b,0x0f,0x4b,0xec,0x77,0xb2,0x7a,0x76,0x14,0xec,0xc8,0x7f,
    0xf4,0xec,0x28,0xf5,0xf0,0x7c,0x86,0xb2,0x7a,0xd8,0x25,0xf1,0xdf,0x23,0xfe,0x28,
    0xdf,0xf4,0x63,0x60,0x16,0x6b,0x14,0xdb,0xfb,0x7d,0x9c,0x7e,0x8f,0x49,0xbf,0xdf,
    0x07,0xf7,0x7e,0x9f,0x00,0xf3,0x7e,0x8f,0x4b,0x9d,0xcd,0x77,0x32,0xac,0xb3,0xe8,
    0x38,0x49,0x1c,0x3b,0xf7,0x34,0xdf,0x58,0x9b,0x63,0xa7,0xe2,0x9a,0x64,0xb0,0xd3,
    0xf2,0x0d,0x77,0xee,0x07,0xb3,0xdc,0x82,0xd8,0xfe,0x10,0xec,0x8c,0xd4,0x24,0x21,
    0x35,0x19,0x85,0x63,0x35,0x18,0xe7,0xec,0xb3,0x92,0xd7,0x47,0xe4,0x75,0x4e,0xf2,
    0xfa,0x18,0xdc,0xf3,0xfa,0x04,0xcc,0xf3,0x9a,0x90,0xbc,0xcc,0x77,0x3e,0xac,0x8b,
    0xc4,0x3e,0x4f,0x6c,0xfb,0x9f,0xe6,0x33,0xb4,0x76,0x4b,0xec,0xcf,0xc1,0x9d,0x73,
    0x01,0x8e,0x7f,0x0f,0x2f,0xc0,0x31,0xbd,0x13,0xc4,0xbc,0x28,0xf3,0xee,0x0b,0xf4,
    0x5e,0x96,0x3b,0xf9,0x25,0xb8,0xed,0xbf,0x84,0xed,0xbe,0xaf,0xf8,0x7f,0xe5,0x57,
    0x66,0xeb,0xd7,0x60,0x9d,0x92,0xdf,0x37,0xe0,0xad,0xe4,0x37,0x29,0xf9,0x99,0xef,
    0x4a,0x58,0x93,0x68,0xb9,0x22,0xda,0xaf,0xa2,0xdd,0xbf,0x85,0x57,0xc9,0xc3,0xb4,
    0x3b,0x7f,0x52,0xb4,0x7c,0x9b,0xa5,0xe5,0x3b,0xb0,0x2a,0xd1,0xf2,0x3d,0xb8,0x6b,
    0x99,0x12,0x2d,0xe6,0xbb,0x16,0xd6,0x14,0xb1,0xaf,0x89,0x96,0x1b,0x68,0xb9,0x8e,
    0x96,0x1b,0xa2,0xc5,0xf9,0x53,0xa2,0xe5,0x87,0x2c,0x2d,0x3f,0x82,0x8d,0x89,0x96,
    0x9f,0xc0,0x5d,0xcb,0xb4,0x68,0x31,0xdf,0xcd,0x98,0x9b,0x89,0x7d,0x53,0xb4,0xdc,
    0x92,0xbe,0x9b,0x96,0x5b,0xd4,0xc8,0xb4,0x38,0x7f,0x3a,0xab,0xc7,0x13,0x59,0x77,
    0x74,0x5c,0xee,0xfc,0xcf,0xc4,0xb3,0x58,0xb7,0xb1,0xff,0x0e,0x93,0x65,0x7d,0x58,
    0xff,0x00,0xa6,0x10,0xcb,0x8b,0xd8,0x0f,0x00,0x00
};

// Generated from:
//
// #version 450 core
//
// #extension GL_EXT_samplerless_texture_functions : require
//
// layout(set = 0, binding = 0)uniform itexture3D src;
// layout(location = 0)out uvec4 dst;
//
// layout(push_constant)uniform PushConstants {
//
//     ivec2 srcOffset;
//     ivec2 dstOffset;
//     int srcMip;
//     int srcLayer;
//
//     bool flipX;
//     bool flipY;
//
//     bool premultiplyAlpha;
//     bool unmultiplyAlpha;
//
//     bool dstHasLuminance;
//     bool dstIsAlpha;
//
//     bool srcIsSRGB;
//     bool dstIsSRGB;
//
//     int dstDefaultChannelsMask;
//     bool rotateXY;
// } params;
//
// void main()
// {
//     ivec2 dstSubImageCoords = ivec2(gl_FragCoord . xy)- params . dstOffset;
//
//     ivec2 srcSubImageCoords = dstSubImageCoords;
//
//     if(params . flipX)
//     {
//         srcSubImageCoords . x = - srcSubImageCoords . x;
//     }
//     if(params . flipY)
//     {
//         srcSubImageCoords . y = - srcSubImageCoords . y;
//     }
//     if(params . rotateXY)
//     {
//         srcSubImageCoords . xy = srcSubImageCoords . yx;
//     }
//
//           ivec4 srcValue = texelFetch(src, ivec3(params . srcOffset + srcSubImageCoords, params . srcLayer), params . srcMip);
//
//     if(params . premultiplyAlpha)
//     {
//         srcValue . rgb *= srcValue . a;
//     }
//     else if(params . unmultiplyAlpha && srcValue . a > 0)
//     {
//         srcValue . rgb /= srcValue . a;
//     }
//
//            uvec4 dstValue = uvec4(srcValue);
//
//     if(params . dstHasLuminance)
//     {
//         dstValue . rg = dstValue . ra;
//     }
//     else if(params . dstIsAlpha)
//     {
//         dstValue . r = dstValue . a;
//     }
//     else
//     {
//         int defaultChannelsMask = params . dstDefaultChannelsMask;
//         if((defaultChannelsMask & 2)!= 0)
//         {
//             dstValue . g = 0;
//         }
//         if((defaultChannelsMask & 4)!= 0)
//         {
//             dstValue . b = 0;
//         }
//         if((defaultChannelsMask & 8)!= 0)
//         {
//             dstValue . a = 1;
//         }
//     }
//
//     dst = dstValue;
// }