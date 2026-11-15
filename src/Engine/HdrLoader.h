//-------------------------------------------------
//
//          HdrLoader.h
//
//  Radiance HDR (.hdr / RGBE 形式) の読み込み。
//-------------------------------------------------

//
// 利用側が守るべき戻り値の画素形式:
//
//   ・返ってくるピクセルは RGBA の float 4 成分 × width × height
//   ・値はリニア空間で、1.0 を超える(それが HDR である理由)
//   ・α は常に 1.0(DXGI_FORMAT_R32G32B32A32_FLOAT にそのまま渡せる形)

#pragma once

#include <cstdint>
#include <vector>

struct HdrImage
{
    uint32_t           width  = 0;
    uint32_t           height = 0;
    std::vector<float> pixels;   // RGBA × width × height(リニア、値は 1.0 超あり)

    bool Valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// 成功したら true。失敗したら out は空のまま。
bool LoadHdr(const char* path, HdrImage& out);
