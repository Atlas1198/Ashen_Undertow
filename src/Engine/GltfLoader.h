//----------------------------------------------------------------------------
// GltfLoader.h
// glTF 2.0 (.gltf / .glb) の読み込み。
//
// これはファイルパースの配管なので、実装の中身は読み飛ばして構わない。
// 使う側が知っておくべきことだけ書いておく:
//
// 【座標系】
//   glTF は右手座標系(Y 上、-Z 前方)だが、このエンジンは左手座標系。
//   ローダー内で Z を反転し、三角形のワインディングも反転済みなので、
//   読み込んだデータはそのまま左手系として扱ってよい。
//
// 【ノード変換】
//   シーングラフのノード変換は頂点に焼き込み済み。
//   描画側はワールド行列を単位行列にしてよい。
//
// 【テクスチャの色空間】
//   baseColor と emissive は sRGB、それ以外(normal / metallicRoughness /
//   occlusion)はリニア。SRV を作るときにフォーマットで区別すること。
//   例: baseColor は DXGI_FORMAT_R8G8B8A8_UNORM_SRGB、
//       normal は DXGI_FORMAT_R8G8B8A8_UNORM
//
// 【タンジェント】
//   glTF が TANGENT を持っていない場合は UV から自動生成する。
//----------------------------------------------------------------------------
#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

inline constexpr uint32_t GltfMaxSkinJoints = 80;

struct GltfVertex
{
    float position[3];
    float normal[3];
    float tangent[4];   // xyz = 接線、w = 従法線の向き(±1)
    float uv[2];
    uint32_t joints[4];
    float weights[4];
};

struct GltfImage
{
    uint32_t             width  = 0;
    uint32_t             height = 0;
    std::vector<uint8_t> pixels;   // RGBA8(1 ピクセル 4 バイト)

    bool Valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

struct GltfMaterial
{
    // 透明の扱い方:
    //   Opaque : アルファを無視して不透明として描く
    //   Mask   : alphaCutoff 未満のピクセルを捨てる(葉や金網に使う)
    //   Blend  : 半透明合成(描画順の管理が要るため、このエンジンは未対応)
    enum class AlphaMode { Opaque, Mask, Blend };

    AlphaMode alphaMode   = AlphaMode::Opaque;
    float     alphaCutoff = 0.5f;
    // 裏面も描くか。葉のような薄い板は両面から見える必要がある
    bool      doubleSided = false;

    float baseColorFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float emissiveFactor[3]  = { 0.0f, 0.0f, 0.0f };
    float metallicFactor     = 1.0f;
    float roughnessFactor    = 1.0f;

    // GltfModel::images への添字。-1 なら「そのテクスチャは無い」
    int baseColorImage         = -1;   // sRGB
    int metallicRoughnessImage = -1;   // リニア(G = roughness, B = metallic)
    int normalImage            = -1;   // リニア(接空間ノーマルマップ)
    int occlusionImage         = -1;   // リニア(R = AO)
    int emissiveImage          = -1;   // sRGB
};

struct GltfPrimitive
{
    std::vector<GltfVertex> vertices;
    std::vector<uint32_t>   indices;
    int                     material = -1;   // materials への添字
    int                     skin = -1;
};

struct GltfNode
{
    std::string name;
    int parent = -1;
    DirectX::XMFLOAT3 translation { 0, 0, 0 };
    DirectX::XMFLOAT4 rotation { 0, 0, 0, 1 };
    DirectX::XMFLOAT3 scale { 1, 1, 1 };
};

struct GltfSkin
{
    std::vector<int> joints;
    std::vector<DirectX::XMFLOAT4X4> inverseBindMatrices;
};

struct GltfAnimationChannel
{
    enum class Path { Translation, Rotation, Scale };
    int node = -1;
    Path path = Path::Translation;
    std::vector<float> times;
    std::vector<DirectX::XMFLOAT4> values;
};

struct GltfAnimation
{
    std::string name;
    std::vector<GltfAnimationChannel> channels;
    float startTime = 0;
    float endTime = 0;
};

struct GltfModel
{
    std::vector<GltfPrimitive> primitives;
    std::vector<GltfMaterial>  materials;
    std::vector<GltfImage>     images;
    std::vector<GltfNode> nodes;
    std::vector<GltfSkin> skins;
    std::vector<GltfAnimation> animations;

    bool Valid() const { return !primitives.empty(); }
};

bool LoadGltf(const char* path, GltfModel& out);
// PNG/JPEGを既存のWICデコーダでRGBA8へ読み込む。
bool LoadTextureImage(const char* path, GltfImage& out);
