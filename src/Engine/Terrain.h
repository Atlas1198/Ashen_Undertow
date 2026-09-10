#pragma once

#include <algorithm>
#include <cmath>

// 描画メッシュと接地が共有する高さ。単位はワールド座標。
namespace Terrain
{
    inline constexpr int Cells = 128;
    inline constexpr float HalfSize = 32.0f;
    inline constexpr float CellSize = HalfSize * 2.0f / Cells;
    inline constexpr float WalkableRadius = 23.0f;
    inline constexpr float BaseHeight = -1.05f;

    inline float VertexHeight(int x, int z)
    {
        const float px = x * CellSize - HalfSize;
        const float pz = z * CellSize - HalfSize;
        const float radius = std::sqrt(px * px + pz * pz);
        const float t = std::clamp((radius - 10.0f) / 18.0f, 0.0f, 1.0f);
        const float rim = t * t * (3.0f - 2.0f * t);
        const float ripples = 0.15f * std::sin(px * 0.48f) * std::sin(pz * 0.43f)
            + 0.09f * std::sin(px * 0.3f + pz * 0.2f);
        const float hills = 3.5f + 1.3f * std::sin(px * 0.24f + pz * 0.17f)
            + 0.9f * std::cos(pz * 0.31f - px * 0.11f)
            + 0.4f * std::sin(px * 0.8f) * std::cos(pz * 0.65f);
        return BaseHeight + ripples + rim * hills;
    }

    // CreateFloorと同じ対角線で三角形補間する（双線形補間ではない）。
    inline float HeightAt(float x, float z)
    {
        const float gx = std::clamp((x + HalfSize) / CellSize, 0.0f, float(Cells));
        const float gz = std::clamp((z + HalfSize) / CellSize, 0.0f, float(Cells));
        const int ix = (std::min)(int(gx), Cells - 1);
        const int iz = (std::min)(int(gz), Cells - 1);
        const float u = gx - ix, v = gz - iz;
        const float a = VertexHeight(ix, iz);
        const float c = VertexHeight(ix + 1, iz + 1);
        if (v >= u)
            return a * (1 - v) + VertexHeight(ix, iz + 1) * (v - u) + c * u;
        return a * (1 - u) + c * v + VertexHeight(ix + 1, iz) * (u - v);
    }

    inline void ClampToArena(float& x, float& z)
    {
        const float radius = std::sqrt(x * x + z * z);
        if (radius > WalkableRadius)
        {
            x *= WalkableRadius / radius;
            z *= WalkableRadius / radius;
        }
    }
}
