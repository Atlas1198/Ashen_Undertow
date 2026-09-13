#pragma once

// Renderer実装ファイル間だけで共有するGPU定数・補助関数。
#include "Renderer.h"
#include "ModelManager.h"
#include "HdrLoader.h"
#include <d3dcompiler.h>
#include <map>
#include <utility>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>

namespace RendererDetail
{
    using namespace DirectX;
    // 頂点フォーマット。
// GltfVertex とメモリ配置を完全に一致させてあるので、
// ローダーの出力をそのまま memcpy できる
    struct Vertex
    {
        XMFLOAT3 position;   // offset  0
        XMFLOAT3 normal;     // offset 12
        XMFLOAT4 tangent;    // offset 24 : xyz = 接線、w = 従法線の向き
        XMFLOAT2 uv;         // offset 40
        XMUINT4 joints;      // offset 48
        XMFLOAT4 weights;    // offset 64
    };                       // 合計 80 バイト

    static_assert(sizeof(Vertex) == sizeof(GltfVertex),
        "Vertex と GltfVertex のメモリ配置が一致していません");

    // 定数バッファの中身。HLSL の cbuffer SceneConstants と
   // メンバの並び・型を一致させること。
    struct SceneConstants
    {
        XMFLOAT4X4 mvp;
        XMFLOAT4X4 world;
		XMFLOAT4X4 invViewProj; // 視点位置の計算に必要なので、ViewProj の逆行列も渡す

        XMFLOAT3 lightDirection;
		float padding0; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
        XMFLOAT3 lightColor;
        float padding1;
        XMFLOAT3 ambientColor;
		float padding2;

        // --- 視点(鏡面反射の計算に必要) ---
        XMFLOAT3 cameraPosition;
		float padding3;

        // --- マテリアル ---
		XMFLOAT3 albedo;    //非金属では拡散色、金属では反射色
		float metallic;     //0 = 非金属、1 = 金属
		float roughness;    //0 = 鏡面、1 = ざらざら
		float padding4; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		float padding5; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		float padding6; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		XMFLOAT3 emissiveFactor;   // 自己発光色
		float padding7; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
        XMFLOAT4X4 lightViewProj;   // 64 : ライト視点のビュー射影行列
        float    alphaCutoff;        //  4 : 負なら常に通る
        float    materialOpacity;    // 半透明描画時の不透明度
        float    windTime;
        float    windStrength;
        uint32_t jointCount;
        XMFLOAT3 windDirection;
        uint32_t renderFlags;
        float padding12[3];
        XMFLOAT4 grassInteractors[2]; // xyz=中心、w=接触半径
        XMFLOAT2 waterInteractionStrength;
        XMFLOAT2 padding13;
        XMFLOAT4X4 jointMatrices[GltfMaxSkinJoints];
    };

    static_assert(sizeof(SceneConstants) % 16 == 0,
        "SceneConstants は 16 バイト境界に揃っていません");

    // HRESULT のチェックを簡潔に書くためのヘルパ
    inline bool Check(HRESULT hr) { return SUCCEEDED(hr); }

    // sRGB エンコードされた値 ↔ リニア値
 //
 // 入力が uint8_t なら取りうる値は 256 通りしかない。
 // 毎回 powf を呼ぶ代わりに、初回に一度だけ表を作って引く。
 // powf は数十サイクルかかるので、ミップ生成のような
 // 数億回のループでは効き方が桁違いになる
    inline const float* SrgbToLinearTable()
    {
        static float table[256];
        static bool  initialized = false;
        if (!initialized)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float c = i / 255.0f;
                table[i] = (c <= 0.04045f) ? c / 12.92f
                    : powf((c + 0.055f) / 1.055f, 2.4f);
            }
            initialized = true;
        }
        return table;
    }

    // 逆方向は入力が連続値だが、出力は 8bit に丸められる。
// ならば入力を量子化して引いても誤差は見えない。
//
// 添字に sqrt を使うのは、リニア値を等間隔で刻むと
// 暗部の精度が落ちるため。sqrt をかませると人間の知覚に近い
// 分布になり、少ない要素数でも破綻しない。
// sqrtf は CPU の命令 1 つで済むので powf より桁違いに軽い
    inline uint8_t LinearToSrgbByte(float c)
    {
        static uint8_t table[4096];
        static bool    initialized = false;
        if (!initialized)
        {
            for (int i = 0; i < 4096; ++i)
            {
                const float t = i / 4095.0f;
                const float lin = t * t;             // sqrt で引くので 2 乗して戻す
                const float s = (lin <= 0.0031308f)
                    ? lin * 12.92f
                    : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
                table[i] = (uint8_t)(s * 255.0f + 0.5f);
            }
            initialized = true;
        }

        if (c <= 0.0f) return 0;
        if (c >= 1.0f) return 255;
        return table[(int)(sqrtf(c) * 4095.0f + 0.5f)];
    }
}
