//-------------------------------------------------
//
//          GltfLoader.cpp
//
//  glTF 2.0 のパースと、DX12 で使える形への変換。
//-------------------------------------------------

//
// 対応範囲:
//   ・.gltf(外部 .bin / 外部画像)と .glb(単一ファイル)
//   ・data: URI(base64 埋め込み)
//   ・POSITION / NORMAL / TANGENT / TEXCOORD_0 / JOINTS_0 / WEIGHTS_0
//   ・pbrMetallicRoughness マテリアル
//   ・シーングラフのノード変換(頂点に焼き込む)
//   ・Skin と線形キーフレームアニメーション
// 非対応:
//   ・sparse accessor、Draco 圧縮、CUBICSPLINE 補間

#include "GltfLoader.h"
#include "Json.h"

#include <Windows.h>
#include <wincodec.h>          // WIC: Windows 標準の画像デコーダ
#include <wrl/client.h>
#include <DirectXMath.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <limits>
#include <map>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace
{
    //------------------------------------------------------------------------
    // ファイル全体をバイト列として読む
    //------------------------------------------------------------------------
    bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out)
    {
        FILE* fp = nullptr;
        if (fopen_s(&fp, path.c_str(), "rb") != 0 || fp == nullptr) return false;

        fseek(fp, 0, SEEK_END);
        const long size = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (size <= 0) { fclose(fp); return false; }

        out.resize((size_t)size);
        const size_t read = fread(out.data(), 1, (size_t)size, fp);
        fclose(fp);
        return read == (size_t)size;
    }

    std::string GetDirectory(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
    }

    //------------------------------------------------------------------------
    // URI のパーセントエンコード(%20 など)を戻す
    //------------------------------------------------------------------------
    std::string DecodeUriPath(const std::string& uri)
    {
        std::string out;
        out.reserve(uri.size());
        for (size_t i = 0; i < uri.size(); ++i)
        {
            if (uri[i] == '%' && i + 2 < uri.size())
            {
                const std::string hex = uri.substr(i + 1, 2);
                out.push_back((char)strtol(hex.c_str(), nullptr, 16));
                i += 2;
            }
            else
            {
                out.push_back(uri[i]);
            }
        }
        return out;
    }

    //------------------------------------------------------------------------
    // base64 デコード(data: URI 用)
    //------------------------------------------------------------------------
    bool DecodeBase64(const char* text, size_t length, std::vector<uint8_t>& out)
    {
        auto value = [](char c) -> int
        {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };

        out.clear();
        out.reserve(length * 3 / 4);

        int buffer = 0, bits = 0;
        for (size_t i = 0; i < length; ++i)
        {
            const int v = value(text[i]);
            if (v < 0) continue;   // '=' や改行は読み飛ばす

            buffer = (buffer << 6) | v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                out.push_back((uint8_t)((buffer >> bits) & 0xFF));
            }
        }
        return true;
    }

    //------------------------------------------------------------------------
    // WIC による画像デコード(PNG / JPEG など)
    //
    // OS の機能を使うので、PNG の zlib 展開や JPEG の DCT を
    // 自前で書く必要がない
    //------------------------------------------------------------------------
    bool DecodeImage(const uint8_t* data, size_t size, GltfImage& out)
    {
        // COM の初期化。すでに初期化済みなら S_FALSE や
        // RPC_E_CHANGED_MODE が返るが、どちらも問題ない
        static bool s_comInitialized = false;
        if (!s_comInitialized)
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            s_comInitialized = true;
        }

        ComPtr<IWICImagingFactory> factory;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
        {
            return false;
        }

        ComPtr<IWICStream> stream;
        if (FAILED(factory->CreateStream(&stream))) return false;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), (DWORD)size)))
            return false;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromStream(
                stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
        {
            return false;
        }

        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame))) return false;

        // どんな入力フォーマットでも RGBA8 に統一して受け取る
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter))) return false;
        if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeMedianCut)))
        {
            return false;
        }

        UINT w = 0, h = 0;
        if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0) return false;

        out.width  = w;
        out.height = h;
        out.pixels.resize((size_t)w * (size_t)h * 4u);
        return SUCCEEDED(converter->CopyPixels(
            nullptr, w * 4, (UINT)out.pixels.size(), out.pixels.data()));
    }

    //------------------------------------------------------------------------
    // アクセサの読み出し
    //------------------------------------------------------------------------
    int ComponentCount(const std::string& type)
    {
        if (type == "SCALAR") return 1;
        if (type == "VEC2")   return 2;
        if (type == "VEC3")   return 3;
        if (type == "VEC4")   return 4;
        if (type == "MAT4")   return 16;
        return 0;
    }

    int ComponentSize(int componentType)
    {
        switch (componentType)
        {
        case 5120: case 5121: return 1;   // BYTE / UNSIGNED_BYTE
        case 5122: case 5123: return 2;   // SHORT / UNSIGNED_SHORT
        case 5125: case 5126: return 4;   // UNSIGNED_INT / FLOAT
        default: return 0;
        }
    }

    struct GltfContext
    {
        const JsonValue*                    root = nullptr;
        std::vector<std::vector<uint8_t>>   buffers;
    };

    // アクセサを float 配列として読み出す(1 要素あたり componentCount 個)
    bool ReadAccessorFloats(const GltfContext& ctx, int accessorIndex,
                            std::vector<float>& out, int& componentCount)
    {
        const JsonValue& accessors = (*ctx.root)["accessors"];
        if (accessorIndex < 0 || (size_t)accessorIndex >= accessors.Size()) return false;

        const JsonValue& accessor = accessors[(size_t)accessorIndex];
        const int  count         = accessor["count"].AsInt();
        const int  componentType = accessor["componentType"].AsInt();
        const std::string type   = accessor["type"].AsString();

        componentCount = ComponentCount(type);
        const int compSize = ComponentSize(componentType);
        if (componentCount == 0 || compSize == 0 || count <= 0) return false;

        out.assign((size_t)count * (size_t)componentCount, 0.0f);

        // bufferView が無い場合はゼロ埋め(仕様上ありうる)
        if (!accessor.Has("bufferView")) return true;

        const JsonValue& view =
            (*ctx.root)["bufferViews"][(size_t)accessor["bufferView"].AsInt()];
        const int bufferIndex = view["buffer"].AsInt();
        if (bufferIndex < 0 || (size_t)bufferIndex >= ctx.buffers.size()) return false;

        const std::vector<uint8_t>& buffer = ctx.buffers[(size_t)bufferIndex];

        const size_t viewOffset     = (size_t)view["byteOffset"].AsInt(0);
        const size_t accessorOffset = (size_t)accessor["byteOffset"].AsInt(0);
        const size_t elementSize    = (size_t)compSize * (size_t)componentCount;
        // byteStride が指定されていればインターリーブ配置
        const size_t stride = view.Has("byteStride")
                            ? (size_t)view["byteStride"].AsInt()
                            : elementSize;

        const bool normalized = accessor["normalized"].AsBool(false);

        for (int i = 0; i < count; ++i)
        {
            const size_t base = viewOffset + accessorOffset + (size_t)i * stride;
            if (base + elementSize > buffer.size()) return false;

            for (int c = 0; c < componentCount; ++c)
            {
                const uint8_t* p = buffer.data() + base + (size_t)c * (size_t)compSize;
                float v = 0.0f;

                switch (componentType)
                {
                case 5126: memcpy(&v, p, 4); break;                        // FLOAT
                case 5125: v = (float)(*(const uint32_t*)p); break;        // UINT
                case 5123:
                {
                    const uint16_t u = *(const uint16_t*)p;                // USHORT
                    v = normalized ? (float)u / 65535.0f : (float)u;
                    break;
                }
                case 5122:
                {
                    const int16_t s = *(const int16_t*)p;                  // SHORT
                    v = normalized ? (float)s / 32767.0f : (float)s;
                    break;
                }
                case 5121:
                {
                    const uint8_t u = *p;                                  // UBYTE
                    v = normalized ? (float)u / 255.0f : (float)u;
                    break;
                }
                case 5120:
                {
                    const int8_t s = *(const int8_t*)p;                    // BYTE
                    v = normalized ? (float)s / 127.0f : (float)s;
                    break;
                }
                default: return false;
                }

                out[(size_t)i * (size_t)componentCount + (size_t)c] = v;
            }
        }
        return true;
    }

    // インデックスアクセサを uint32 として読み出す
    bool ReadAccessorIndices(const GltfContext& ctx, int accessorIndex,
                             std::vector<uint32_t>& out)
    {
        std::vector<float> temp;
        int componentCount = 0;
        if (!ReadAccessorFloats(ctx, accessorIndex, temp, componentCount)) return false;
        if (componentCount != 1) return false;

        out.resize(temp.size());
        for (size_t i = 0; i < temp.size(); ++i) out[i] = (uint32_t)temp[i];
        return true;
    }

    //------------------------------------------------------------------------
    // タンジェント生成(glTF が TANGENT を持っていない場合)
    //
    // 三角形ごとに「UV の u が増える方向」を求め、頂点に加算していく。
    // 最後に法線と直交化して正規化する
    //------------------------------------------------------------------------
    void GenerateTangents(GltfPrimitive& prim)
    {
        std::vector<XMFLOAT3> accum(prim.vertices.size(), XMFLOAT3(0, 0, 0));

        for (size_t i = 0; i + 2 < prim.indices.size(); i += 3)
        {
            const uint32_t i0 = prim.indices[i + 0];
            const uint32_t i1 = prim.indices[i + 1];
            const uint32_t i2 = prim.indices[i + 2];
            if (i0 >= prim.vertices.size() || i1 >= prim.vertices.size() ||
                i2 >= prim.vertices.size())
            {
                continue;
            }

            const GltfVertex& v0 = prim.vertices[i0];
            const GltfVertex& v1 = prim.vertices[i1];
            const GltfVertex& v2 = prim.vertices[i2];

            const float e1x = v1.position[0] - v0.position[0];
            const float e1y = v1.position[1] - v0.position[1];
            const float e1z = v1.position[2] - v0.position[2];
            const float e2x = v2.position[0] - v0.position[0];
            const float e2y = v2.position[1] - v0.position[1];
            const float e2z = v2.position[2] - v0.position[2];

            const float du1 = v1.uv[0] - v0.uv[0];
            const float dv1 = v1.uv[1] - v0.uv[1];
            const float du2 = v2.uv[0] - v0.uv[0];
            const float dv2 = v2.uv[1] - v0.uv[1];

            const float det = du1 * dv2 - du2 * dv1;
            if (fabsf(det) < 1e-12f) continue;
            const float r = 1.0f / det;

            const XMFLOAT3 t(
                (dv2 * e1x - dv1 * e2x) * r,
                (dv2 * e1y - dv1 * e2y) * r,
                (dv2 * e1z - dv1 * e2z) * r);

            for (uint32_t idx : { i0, i1, i2 })
            {
                accum[idx].x += t.x;
                accum[idx].y += t.y;
                accum[idx].z += t.z;
            }
        }

        for (size_t i = 0; i < prim.vertices.size(); ++i)
        {
            GltfVertex& v = prim.vertices[i];
            XMVECTOR n = XMVectorSet(v.normal[0], v.normal[1], v.normal[2], 0.0f);
            XMVECTOR t = XMVectorSet(accum[i].x, accum[i].y, accum[i].z, 0.0f);

            // 縮退していたら適当な直交ベクトルで代用する
            if (XMVectorGetX(XMVector3LengthSq(t)) < 1e-16f)
            {
                const XMVECTOR up = fabsf(v.normal[1]) < 0.99f
                                  ? XMVectorSet(0, 1, 0, 0)
                                  : XMVectorSet(1, 0, 0, 0);
                t = XMVector3Cross(up, n);
            }

            // グラム・シュミット直交化
            t = XMVector3Normalize(XMVectorSubtract(t, XMVectorScale(n, XMVectorGetX(XMVector3Dot(n, t)))));

            XMFLOAT3 result;
            XMStoreFloat3(&result, t);
            v.tangent[0] = result.x;
            v.tangent[1] = result.y;
            v.tangent[2] = result.z;
            v.tangent[3] = 1.0f;
        }
    }

    //------------------------------------------------------------------------
    // ノードの行列を求める(行ベクトル規約)
    //------------------------------------------------------------------------
    XMMATRIX GetNodeMatrix(const JsonValue& node)
    {
        if (node.Has("matrix"))
        {
            const JsonValue& m = node["matrix"];
            // glTF の行列は列優先。列ベクトル規約の M をそのまま並べると、
            // 行ベクトル規約では転置に相当する = この順で行に詰めればよい
            return XMMatrixSet(
                m[0].AsFloat(),  m[1].AsFloat(),  m[2].AsFloat(),  m[3].AsFloat(),
                m[4].AsFloat(),  m[5].AsFloat(),  m[6].AsFloat(),  m[7].AsFloat(),
                m[8].AsFloat(),  m[9].AsFloat(),  m[10].AsFloat(), m[11].AsFloat(),
                m[12].AsFloat(), m[13].AsFloat(), m[14].AsFloat(), m[15].AsFloat());
        }

        XMMATRIX result = XMMatrixIdentity();

        if (node.Has("scale"))
        {
            const JsonValue& s = node["scale"];
            result = XMMatrixScaling(s[0].AsFloat(1), s[1].AsFloat(1), s[2].AsFloat(1));
        }
        if (node.Has("rotation"))
        {
            const JsonValue& r = node["rotation"];   // クォータニオン xyzw
            const XMVECTOR q = XMVectorSet(r[0].AsFloat(), r[1].AsFloat(),
                                           r[2].AsFloat(), r[3].AsFloat(1));
            result = XMMatrixMultiply(result, XMMatrixRotationQuaternion(q));
        }
        if (node.Has("translation"))
        {
            const JsonValue& t = node["translation"];
            result = XMMatrixMultiply(result,
                XMMatrixTranslation(t[0].AsFloat(), t[1].AsFloat(), t[2].AsFloat()));
        }
        return result;
    }

    //------------------------------------------------------------------------
    // プリミティブ 1 個を読み込む
    //------------------------------------------------------------------------
    bool LoadPrimitive(const GltfContext& ctx, const JsonValue& primJson,
                       const XMMATRIX& world, GltfPrimitive& out)
    {
        const JsonValue& attributes = primJson["attributes"];
        if (!attributes.Has("POSITION")) return false;

        // mode 4 = TRIANGLES 以外は非対応(既定値も 4)
        if (primJson["mode"].AsInt(4) != 4) return false;

        std::vector<float> positions, normals, tangents, uvs, joints, weights;
        int cc = 0;

        if (!ReadAccessorFloats(ctx, attributes["POSITION"].AsInt(), positions, cc) || cc != 3)
            return false;

        const size_t vertexCount = positions.size() / 3;

        bool hasNormals = false;
        if (attributes.Has("NORMAL"))
            hasNormals = ReadAccessorFloats(ctx, attributes["NORMAL"].AsInt(), normals, cc) && cc == 3;

        bool hasTangents = false;
        if (attributes.Has("TANGENT"))
            hasTangents = ReadAccessorFloats(ctx, attributes["TANGENT"].AsInt(), tangents, cc) && cc == 4;

        bool hasUvs = false;
        if (attributes.Has("TEXCOORD_0"))
            hasUvs = ReadAccessorFloats(ctx, attributes["TEXCOORD_0"].AsInt(), uvs, cc) && cc == 2;

        bool hasSkin = false;
        if (attributes.Has("JOINTS_0") && attributes.Has("WEIGHTS_0"))
        {
            hasSkin = ReadAccessorFloats(ctx, attributes["JOINTS_0"].AsInt(), joints, cc) && cc == 4;
            hasSkin = hasSkin &&
                ReadAccessorFloats(ctx, attributes["WEIGHTS_0"].AsInt(), weights, cc) && cc == 4;
        }

        // 法線変換用の逆転置行列(非等倍スケールに対応するため)
        XMVECTOR det;
        const XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(&det, world));

        out.vertices.resize(vertexCount);
        for (size_t i = 0; i < vertexCount; ++i)
        {
            GltfVertex& v = out.vertices[i];

            XMVECTOR p = XMVectorSet(positions[i * 3 + 0], positions[i * 3 + 1],
                                     positions[i * 3 + 2], 1.0f);
            p = XMVector3Transform(p, world);
            // 右手系 → 左手系: Z を反転する
            v.position[0] =  XMVectorGetX(p);
            v.position[1] =  XMVectorGetY(p);
            v.position[2] = -XMVectorGetZ(p);

            if (hasNormals)
            {
                XMVECTOR n = XMVectorSet(normals[i * 3 + 0], normals[i * 3 + 1],
                                         normals[i * 3 + 2], 0.0f);
                n = XMVector3Normalize(XMVector3TransformNormal(n, normalMatrix));
                v.normal[0] =  XMVectorGetX(n);
                v.normal[1] =  XMVectorGetY(n);
                v.normal[2] = -XMVectorGetZ(n);
            }
            else
            {
                v.normal[0] = 0.0f; v.normal[1] = 1.0f; v.normal[2] = 0.0f;
            }

            if (hasTangents)
            {
                XMVECTOR t = XMVectorSet(tangents[i * 4 + 0], tangents[i * 4 + 1],
                                         tangents[i * 4 + 2], 0.0f);
                t = XMVector3Normalize(XMVector3TransformNormal(t, world));
                v.tangent[0] =  XMVectorGetX(t);
                v.tangent[1] =  XMVectorGetY(t);
                v.tangent[2] = -XMVectorGetZ(t);
                // 鏡像変換なので従法線の向きも反転する
                v.tangent[3] = -tangents[i * 4 + 3];
            }
            else
            {
                v.tangent[0] = 1.0f; v.tangent[1] = 0.0f;
                v.tangent[2] = 0.0f; v.tangent[3] = 1.0f;
            }

            v.uv[0] = hasUvs ? uvs[i * 2 + 0] : 0.0f;
            v.uv[1] = hasUvs ? uvs[i * 2 + 1] : 0.0f;
            for (size_t influence = 0; influence < 4; ++influence)
            {
                v.joints[influence] = hasSkin
                    ? static_cast<uint32_t>(joints[i * 4 + influence]) : 0;
                v.weights[influence] = hasSkin ? weights[i * 4 + influence] : 0.0f;
            }
        }

        // インデックス
        if (primJson.Has("indices"))
        {
            if (!ReadAccessorIndices(ctx, primJson["indices"].AsInt(), out.indices))
                return false;
        }
        else
        {
            out.indices.resize(vertexCount);
            for (size_t i = 0; i < vertexCount; ++i) out.indices[i] = (uint32_t)i;
        }

        // Z 反転で鏡像になったぶん、三角形の向きを戻す
        for (size_t i = 0; i + 2 < out.indices.size(); i += 3)
            std::swap(out.indices[i + 1], out.indices[i + 2]);

        if (!hasTangents) GenerateTangents(out);

        out.material = primJson.Has("material") ? primJson["material"].AsInt() : -1;
        return true;
    }

    //------------------------------------------------------------------------
    // ノードを再帰的にたどる
    //------------------------------------------------------------------------
    void TraverseNode(const GltfContext& ctx, int nodeIndex,
                      const XMMATRIX& parentMatrix, GltfModel& out, int depth)
    {
        if (depth > 64) return;   // 循環参照への保険

        const JsonValue& nodes = (*ctx.root)["nodes"];
        if (nodeIndex < 0 || (size_t)nodeIndex >= nodes.Size()) return;

        const JsonValue& node  = nodes[(size_t)nodeIndex];
        const XMMATRIX   world = XMMatrixMultiply(GetNodeMatrix(node), parentMatrix);

        if (node.Has("mesh"))
        {
            const JsonValue& meshes = (*ctx.root)["meshes"];
            const int meshIndex = node["mesh"].AsInt();
            if (meshIndex >= 0 && (size_t)meshIndex < meshes.Size())
            {
                const JsonValue& primitives = meshes[(size_t)meshIndex]["primitives"];
                for (size_t i = 0; i < primitives.Size(); ++i)
                {
                    GltfPrimitive prim;
                    const int skin = node["skin"].AsInt(-1);
                    const XMMATRIX meshTransform = skin >= 0 ? XMMatrixIdentity() : world;
                    if (LoadPrimitive(ctx, primitives[i], meshTransform, prim))
                    {
                        prim.skin = skin;
                        out.primitives.push_back(std::move(prim));
                    }
                }
            }
        }

        const JsonValue& children = node["children"];
        for (size_t i = 0; i < children.Size(); ++i)
            TraverseNode(ctx, children[i].AsInt(), world, out, depth + 1);
    }

    void LoadNodes(const JsonValue& root, GltfModel& out)
    {
        const JsonValue& nodes = root["nodes"];
        out.nodes.resize(nodes.Size());
        for (size_t i = 0; i < nodes.Size(); ++i)
        {
            out.nodes[i].name = nodes[i]["name"].AsString();
            XMVECTOR scale, rotation, translation;
            if (XMMatrixDecompose(&scale, &rotation, &translation, GetNodeMatrix(nodes[i])))
            {
                XMStoreFloat3(&out.nodes[i].scale, scale);
                XMStoreFloat4(&out.nodes[i].rotation, rotation);
                XMStoreFloat3(&out.nodes[i].translation, translation);
            }

            const JsonValue& children = nodes[i]["children"];
            for (size_t child = 0; child < children.Size(); ++child)
            {
                const int childIndex = children[child].AsInt(-1);
                if (childIndex >= 0 && static_cast<size_t>(childIndex) < out.nodes.size())
                    out.nodes[childIndex].parent = static_cast<int>(i);
            }
        }
    }

    void LoadSkins(const GltfContext& ctx, GltfModel& out)
    {
        const JsonValue& skins = (*ctx.root)["skins"];
        out.skins.resize(skins.Size());
        for (size_t i = 0; i < skins.Size(); ++i)
        {
            const JsonValue& joints = skins[i]["joints"];
            auto& skin = out.skins[i];
            skin.joints.reserve(joints.Size());
            for (size_t joint = 0; joint < joints.Size(); ++joint)
                skin.joints.push_back(joints[joint].AsInt(-1));

            std::vector<float> values;
            int componentCount = 0;
            if (skins[i].Has("inverseBindMatrices") &&
                ReadAccessorFloats(ctx, skins[i]["inverseBindMatrices"].AsInt(),
                    values, componentCount) && componentCount == 16)
            {
                skin.inverseBindMatrices.resize(values.size() / 16);
                for (size_t matrix = 0; matrix < skin.inverseBindMatrices.size(); ++matrix)
                    memcpy(&skin.inverseBindMatrices[matrix], values.data() + matrix * 16,
                        sizeof(XMFLOAT4X4));
            }
            else
            {
                skin.inverseBindMatrices.resize(skin.joints.size());
                for (auto& matrix : skin.inverseBindMatrices)
                    XMStoreFloat4x4(&matrix, XMMatrixIdentity());
            }
        }
    }

    void LoadAnimations(const GltfContext& ctx, GltfModel& out)
    {
        const JsonValue& animations = (*ctx.root)["animations"];
        for (size_t animationIndex = 0; animationIndex < animations.Size(); ++animationIndex)
        {
            const JsonValue& source = animations[animationIndex];
            const JsonValue& samplers = source["samplers"];
            GltfAnimation animation;
            animation.name = source["name"].AsString();
            animation.startTime = (std::numeric_limits<float>::max)();

            const JsonValue& channels = source["channels"];
            for (size_t channelIndex = 0; channelIndex < channels.Size(); ++channelIndex)
            {
                const JsonValue& sourceChannel = channels[channelIndex];
                const int samplerIndex = sourceChannel["sampler"].AsInt(-1);
                if (samplerIndex < 0 || static_cast<size_t>(samplerIndex) >= samplers.Size())
                    continue;
                const JsonValue& sampler = samplers[static_cast<size_t>(samplerIndex)];
                if (sampler["interpolation"].AsString("LINEAR") == "CUBICSPLINE") continue;

                GltfAnimationChannel channel;
                channel.node = sourceChannel["target"]["node"].AsInt(-1);
                const std::string path = sourceChannel["target"]["path"].AsString();
                int expectedComponents = 3;
                if (path == "rotation")
                {
                    channel.path = GltfAnimationChannel::Path::Rotation;
                    expectedComponents = 4;
                }
                else if (path == "scale")
                    channel.path = GltfAnimationChannel::Path::Scale;
                else if (path != "translation")
                    continue;

                int componentCount = 0;
                std::vector<float> values;
                if (!ReadAccessorFloats(ctx, sampler["input"].AsInt(),
                        channel.times, componentCount) || componentCount != 1 ||
                    !ReadAccessorFloats(ctx, sampler["output"].AsInt(),
                        values, componentCount) || componentCount != expectedComponents ||
                    values.size() / static_cast<size_t>(componentCount) != channel.times.size())
                {
                    continue;
                }

                channel.values.resize(channel.times.size());
                for (size_t key = 0; key < channel.times.size(); ++key)
                {
                    auto& value = channel.values[key];
                    value.x = values[key * componentCount + 0];
                    value.y = values[key * componentCount + 1];
                    value.z = values[key * componentCount + 2];
                    value.w = componentCount == 4 ? values[key * componentCount + 3] : 0.0f;
                }
                if (!channel.times.empty())
                {
                    animation.startTime = (std::min)(animation.startTime, channel.times.front());
                    animation.endTime = (std::max)(animation.endTime, channel.times.back());
                    animation.channels.push_back(std::move(channel));
                }
            }

            if (!animation.channels.empty())
                out.animations.push_back(std::move(animation));
        }
    }

    //------------------------------------------------------------------------
    // マテリアル
    //------------------------------------------------------------------------
    // texture 添字 → image 添字
    int TextureToImage(const JsonValue& root, int textureIndex)
    {
        if (textureIndex < 0) return -1;
        const JsonValue& textures = root["textures"];
        if ((size_t)textureIndex >= textures.Size()) return -1;
        const JsonValue& tex = textures[(size_t)textureIndex];
        return tex.Has("source") ? tex["source"].AsInt() : -1;
    }

    int TextureRef(const JsonValue& root, const JsonValue& info)
    {
        if (info.IsNull() || !info.Has("index")) return -1;
        return TextureToImage(root, info["index"].AsInt());
    }

    void LoadMaterials(const JsonValue& root, GltfModel& out)
    {
        const JsonValue& materials = root["materials"];
        out.materials.resize(materials.Size());

        for (size_t i = 0; i < materials.Size(); ++i)
        {
            const JsonValue& m = materials[i];
            GltfMaterial& dst = out.materials[i];

            const JsonValue& pbr = m["pbrMetallicRoughness"];
            if (!pbr.IsNull())
            {
                const JsonValue& bcf = pbr["baseColorFactor"];
                if (bcf.IsArray() && bcf.Size() >= 4)
                    for (int c = 0; c < 4; ++c) dst.baseColorFactor[c] = bcf[(size_t)c].AsFloat(1.0f);

                dst.metallicFactor  = pbr["metallicFactor"].AsFloat(1.0f);
                dst.roughnessFactor = pbr["roughnessFactor"].AsFloat(1.0f);

                dst.baseColorImage         = TextureRef(root, pbr["baseColorTexture"]);
                dst.metallicRoughnessImage = TextureRef(root, pbr["metallicRoughnessTexture"]);
            }

            dst.normalImage    = TextureRef(root, m["normalTexture"]);
            dst.occlusionImage = TextureRef(root, m["occlusionTexture"]);
            dst.emissiveImage  = TextureRef(root, m["emissiveTexture"]);

            const JsonValue& ef = m["emissiveFactor"];
            if (ef.IsArray() && ef.Size() >= 3)
                for (int c = 0; c < 3; ++c) dst.emissiveFactor[c] = ef[(size_t)c].AsFloat(0.0f);

            const JsonValue& specular = m["extensions"]["KHR_materials_specular"];
            if (!specular.IsNull())
            {
                dst.specularFactor = specular["specularFactor"].AsFloat(1.0f);
                const JsonValue& color = specular["specularColorFactor"];
                if (color.IsArray() && color.Size() >= 3)
                    for (int c = 0; c < 3; ++c)
                        dst.specularColorFactor[c] = color[(size_t)c].AsFloat(1.0f);
                dst.specularImage = TextureRef(root, specular["specularTexture"]);
            }

            // 透明の扱い。既定は OPAQUE
            const std::string alphaMode = m["alphaMode"].AsString("OPAQUE");
            if      (alphaMode == "MASK")  dst.alphaMode = GltfMaterial::AlphaMode::Mask;
            else if (alphaMode == "BLEND") dst.alphaMode = GltfMaterial::AlphaMode::Blend;
            else                           dst.alphaMode = GltfMaterial::AlphaMode::Opaque;

            dst.alphaCutoff = m["alphaCutoff"].AsFloat(0.5f);
            dst.doubleSided = m["doubleSided"].AsBool(false);
        }
    }
}

//----------------------------------------------------------------------------
bool LoadTextureImage(const char* path, GltfImage& out)
{
    out = {};
    std::vector<uint8_t> bytes;
    return path && ReadFileBytes(path, bytes) && DecodeImage(bytes.data(), bytes.size(), out);
}

bool LoadGltf(const char* path, GltfModel& out)
{
    out = GltfModel{};

    const std::string filePath = path;
    const std::string baseDir  = GetDirectory(filePath);

    std::vector<uint8_t> fileBytes;
    if (!ReadFileBytes(filePath, fileBytes)) return false;

    std::string           jsonText;
    std::vector<uint8_t>  glbBinaryChunk;

    // --- .glb か .gltf かを判定 ---
    if (fileBytes.size() >= 12 && memcmp(fileBytes.data(), "glTF", 4) == 0)
    {
        // GLB: 12 バイトヘッダ + チャンク列(長さ 4 + 種別 4 + データ)
        size_t offset = 12;
        while (offset + 8 <= fileBytes.size())
        {
            uint32_t chunkLength = 0, chunkType = 0;
            memcpy(&chunkLength, fileBytes.data() + offset,     4);
            memcpy(&chunkType,   fileBytes.data() + offset + 4, 4);
            offset += 8;
            if (offset + chunkLength > fileBytes.size()) break;

            if (chunkType == 0x4E4F534A)        // 'JSON'
                jsonText.assign((const char*)fileBytes.data() + offset, chunkLength);
            else if (chunkType == 0x004E4942)   // 'BIN'
                glbBinaryChunk.assign(fileBytes.begin() + offset,
                                      fileBytes.begin() + offset + chunkLength);

            offset += chunkLength;
            offset = (offset + 3) & ~size_t(3);   // 4 バイト境界に揃える
        }
    }
    else
    {
        jsonText.assign((const char*)fileBytes.data(), fileBytes.size());
    }

    if (jsonText.empty()) return false;

    JsonValue root;
    if (!ParseJson(jsonText.c_str(), jsonText.size(), root) || !root.IsObject())
        return false;

    // --- バッファを読み込む ---
    GltfContext ctx;
    ctx.root = &root;

    const JsonValue& buffers = root["buffers"];
    ctx.buffers.resize(buffers.Size());
    for (size_t i = 0; i < buffers.Size(); ++i)
    {
        const JsonValue& b = buffers[i];
        if (!b.Has("uri"))
        {
            // uri 無し = GLB の BIN チャンク
            ctx.buffers[i] = glbBinaryChunk;
            continue;
        }

        const std::string uri = b["uri"].AsString();
        if (uri.compare(0, 5, "data:") == 0)
        {
            const size_t comma = uri.find(',');
            if (comma == std::string::npos) return false;
            DecodeBase64(uri.c_str() + comma + 1, uri.size() - comma - 1, ctx.buffers[i]);
        }
        else
        {
            if (!ReadFileBytes(baseDir + DecodeUriPath(uri), ctx.buffers[i])) return false;
        }
    }

    // --- 画像を読み込む ---
    const JsonValue& images = root["images"];
    out.images.resize(images.Size());
    for (size_t i = 0; i < images.Size(); ++i)
    {
        const JsonValue& img = images[i];

        std::vector<uint8_t> encoded;
        if (img.Has("uri"))
        {
            const std::string uri = img["uri"].AsString();
            if (uri.compare(0, 5, "data:") == 0)
            {
                const size_t comma = uri.find(',');
                if (comma == std::string::npos) continue;
                DecodeBase64(uri.c_str() + comma + 1, uri.size() - comma - 1, encoded);
            }
            else
            {
                if (!ReadFileBytes(baseDir + DecodeUriPath(uri), encoded)) continue;
            }
        }
        else if (img.Has("bufferView"))
        {
            const JsonValue& view = root["bufferViews"][(size_t)img["bufferView"].AsInt()];
            const int bufferIndex = view["buffer"].AsInt();
            if (bufferIndex < 0 || (size_t)bufferIndex >= ctx.buffers.size()) continue;

            const std::vector<uint8_t>& buffer = ctx.buffers[(size_t)bufferIndex];
            const size_t viewOffset = (size_t)view["byteOffset"].AsInt(0);
            const size_t viewLength = (size_t)view["byteLength"].AsInt(0);
            if (viewOffset + viewLength > buffer.size()) continue;

            encoded.assign(buffer.begin() + viewOffset,
                           buffer.begin() + viewOffset + viewLength);
        }

        if (!encoded.empty())
            DecodeImage(encoded.data(), encoded.size(), out.images[i]);
    }

    LoadMaterials(root, out);
    LoadNodes(root, out);
    LoadSkins(ctx, out);
    LoadAnimations(ctx, out);

    // --- シーンをたどってメッシュを集める ---
    const int sceneIndex = root["scene"].AsInt(0);
    const JsonValue& scenes = root["scenes"];

    if (sceneIndex >= 0 && (size_t)sceneIndex < scenes.Size())
    {
        const JsonValue& sceneNodes = scenes[(size_t)sceneIndex]["nodes"];
        for (size_t i = 0; i < sceneNodes.Size(); ++i)
            TraverseNode(ctx, sceneNodes[i].AsInt(), XMMatrixIdentity(), out, 0);
    }
    else
    {
        // scenes が無いファイルへの保険: 全ノードを root として扱う
        const JsonValue& nodes = root["nodes"];
        for (size_t i = 0; i < nodes.Size(); ++i)
            TraverseNode(ctx, (int)i, XMMatrixIdentity(), out, 0);
    }

    return out.Valid();
}
