#include "Terrain.h"
#include <cassert>

bool Near(float a, float b) { return std::abs(a - b) < 0.0001f; }

int main()
{
    assert(Near(Terrain::HeightAt(0, 0), Terrain::BaseHeight));
    float low = 100, high = -100;
    for (int z = 0; z <= Terrain::Cells; ++z)
        for (int x = 0; x <= Terrain::Cells; ++x)
        {
            const float px = x * Terrain::CellSize - Terrain::HalfSize;
            const float pz = z * Terrain::CellSize - Terrain::HalfSize;
            const float h = Terrain::HeightAt(px, pz);
            assert(std::isfinite(h) && Near(h, Terrain::VertexHeight(x, z)));
            low = std::min(low, h);
            high = std::max(high, h);
            if (px * px + pz * pz < 64)
                assert(std::abs(h - Terrain::BaseHeight) < 0.25f);
        }
    assert(high - low > 3.0f);
    // 各三角形の重心が、実際の頂点3つの平均高さと一致。
    for (int z = 0; z < Terrain::Cells; z += 7)
        for (int x = 0; x < Terrain::Cells; x += 7)
        {
            const float px = x * Terrain::CellSize - Terrain::HalfSize;
            const float pz = z * Terrain::CellSize - Terrain::HalfSize;
            const float a = Terrain::VertexHeight(x, z), c = Terrain::VertexHeight(x + 1, z + 1);
            assert(Near(Terrain::HeightAt(px + Terrain::CellSize / 3, pz + Terrain::CellSize * 2 / 3),
                (a + Terrain::VertexHeight(x, z + 1) + c) / 3));
            assert(Near(Terrain::HeightAt(px + Terrain::CellSize * 2 / 3, pz + Terrain::CellSize / 3),
                (a + Terrain::VertexHeight(x + 1, z) + c) / 3));
        }
    float x = 100, z = 100;
    Terrain::ClampToArena(x, z);
    assert(Near(std::sqrt(x * x + z * z), Terrain::WalkableRadius));
    assert(Near(x, z));
    x = 3; z = 4;
    Terrain::ClampToArena(x, z);
    assert(x == 3 && z == 4);
}
