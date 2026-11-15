//-------------------------------------------------
//
//          HdrLoader.cpp
//
//  Radiance HDR (RGBE) のデコード。
//-------------------------------------------------

//
// フォーマットの概要:
//   1. "#?RADIANCE" で始まるヘッダ行
//   2. key=value 形式の行が続き、空行で終わる
//   3. 解像度行 "-Y <height> +X <width>"
//   4. スキャンライン本体。1 ピクセル 4 バイト (R, G, B, 共有指数 E)
//
// RGBE は「RGB 3 成分で 8bit 仮数 + 共通の 8bit 指数」という表現で、
// 32bit float × 3 の 1/3 のサイズで広いダイナミックレンジを表せる。
// 実際の色は  rgb * 2^(E - 128 - 8)  で復元する。

#include "HdrLoader.h"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace
{
    // 改行までを 1 行読む(末尾の改行は含めない)
    bool ReadLine(FILE* fp, char* buf, int maxLen)
    {
        int i = 0;
        for (;;)
        {
            int c = fgetc(fp);
            if (c == EOF) { buf[i] = '\0'; return i > 0; }
            if (c == '\n') break;
            if (i < maxLen - 1) buf[i++] = (char)c;
        }
        buf[i] = '\0';
        return true;
    }

    // RGBE 4 バイト -> リニア float RGBA
    inline void RgbeToFloat(const unsigned char rgbe[4], float* out)
    {
        if (rgbe[3] == 0)
        {
            out[0] = out[1] = out[2] = 0.0f;
            out[3] = 1.0f;
            return;
        }
        // 128 は指数のバイアス、8 は仮数が 8bit 整数であることの補正
        const float f = ldexpf(1.0f, (int)rgbe[3] - (128 + 8));
        out[0] = (float)rgbe[0] * f;
        out[1] = (float)rgbe[1] * f;
        out[2] = (float)rgbe[2] * f;
        out[3] = 1.0f;
    }
}

bool LoadHdr(const char* path, HdrImage& out)
{
    out = HdrImage{};

    FILE* fp = nullptr;
    if (fopen_s(&fp, path, "rb") != 0 || fp == nullptr) return false;

    char line[512];

    // --- マジック行 ---
    if (!ReadLine(fp, line, sizeof(line)) || strncmp(line, "#?", 2) != 0)
    {
        fclose(fp);
        return false;
    }

    // --- ヘッダ(空行まで読み飛ばす) ---
    for (;;)
    {
        if (!ReadLine(fp, line, sizeof(line))) { fclose(fp); return false; }
        if (line[0] == '\0') break;
    }

    // --- 解像度行 ---
    // 一般的な向き指定 "-Y height +X width" のみ対応(実用上これで足りる)
    if (!ReadLine(fp, line, sizeof(line))) { fclose(fp); return false; }
    int w = 0, h = 0;
    if (sscanf_s(line, "-Y %d +X %d", &h, &w) != 2 || w <= 0 || h <= 0)
    {
        fclose(fp);
        return false;
    }

    out.width  = (uint32_t)w;
    out.height = (uint32_t)h;
    out.pixels.assign((size_t)w * (size_t)h * 4u, 0.0f);

    std::vector<unsigned char> scan((size_t)w * 4u);

    for (int y = 0; y < h; ++y)
    {
        unsigned char head[4];
        if (fread(head, 1, 4, fp) != 4) { fclose(fp); return false; }

        // 新形式(アダプティブ RLE)の目印: 2, 2, width の上位/下位バイト
        const bool rle = (head[0] == 2 && head[1] == 2 &&
                          (((int)head[2] << 8) | (int)head[3]) == w &&
                          w >= 8 && w < 32768);

        if (rle)
        {
            // R, G, B, E の 4 成分を「成分ごとに」まとめて RLE 圧縮してある
            for (int c = 0; c < 4; ++c)
            {
                int x = 0;
                while (x < w)
                {
                    int count = fgetc(fp);
                    if (count == EOF) { fclose(fp); return false; }

                    if (count > 128)
                    {
                        // ランレングス: 同じ値が (count - 128) 回続く
                        const int v = fgetc(fp);
                        if (v == EOF) { fclose(fp); return false; }
                        count -= 128;
                        if (x + count > w) { fclose(fp); return false; }
                        for (int k = 0; k < count; ++k)
                            scan[(size_t)(x++) * 4u + (size_t)c] = (unsigned char)v;
                    }
                    else
                    {
                        // リテラル: count 個の値がそのまま並ぶ
                        if (count == 0 || x + count > w) { fclose(fp); return false; }
                        for (int k = 0; k < count; ++k)
                        {
                            const int v = fgetc(fp);
                            if (v == EOF) { fclose(fp); return false; }
                            scan[(size_t)(x++) * 4u + (size_t)c] = (unsigned char)v;
                        }
                    }
                }
            }
        }
        else
        {
            // 旧形式(非圧縮)。読んだ 4 バイトは最初のピクセルそのもの
            scan[0] = head[0]; scan[1] = head[1];
            scan[2] = head[2]; scan[3] = head[3];
            if (w > 1)
            {
                const size_t rest = (size_t)(w - 1) * 4u;
                if (fread(&scan[4], 1, rest, fp) != rest) { fclose(fp); return false; }
            }
        }

        float* dst = &out.pixels[(size_t)y * (size_t)w * 4u];
        for (int x = 0; x < w; ++x)
            RgbeToFloat(&scan[(size_t)x * 4u], dst + (size_t)x * 4u);
    }

    fclose(fp);
    return true;
}
