#include "RendererInternal.h"
#include "Terrain.h"

using namespace DirectX;
using namespace RendererDetail;

bool Renderer::CreateVertexBuffer()
{
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RANGE readRange = { 0, 0 };

    bool hasMesh = false;
    for (auto& gpuModel : m_models)
    {
        const auto& model = *gpuModel.model;
        gpuModel.meshes.reserve(model.primitives.size());
        for (const GltfPrimitive& prim : model.primitives)
        {
            if (prim.vertices.empty() || prim.indices.empty()) continue;

            MeshPrimitive mesh;
            mesh.material = prim.material;
            mesh.indexCount = (UINT)prim.indices.size();

        // マテリアルからアルファと両面の設定を取り出しておく。
        // 描画時にマテリアル配列を引き直さずに済む
            if (prim.material >= 0 && (size_t)prim.material < model.materials.size())
            {
                const GltfMaterial& mat = model.materials[(size_t)prim.material];
            // MASK 以外は「常に通る」ことを表す負の値にする。
            // こうするとシェーダ側に分岐が要らない
                mesh.alphaCutoff = (mat.alphaMode == GltfMaterial::AlphaMode::Mask)
                    ? mat.alphaCutoff : -1.0f;
                mesh.doubleSided = mat.doubleSided;
            }

            const UINT vertexBufferSize = (UINT)(prim.vertices.size() * sizeof(GltfVertex));
            const UINT indexBufferSize = (UINT)(prim.indices.size() * sizeof(uint32_t));

        // --- 頂点バッファ ---
            resourceDesc.Width = vertexBufferSize;
            if (!Check(m_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&mesh.vertexBuffer))))
            {
                return false;
            }

            void* mapped = nullptr;
            if (!Check(mesh.vertexBuffer->Map(0, &readRange, &mapped))) return false;
            memcpy(mapped, prim.vertices.data(), vertexBufferSize);
            mesh.vertexBuffer->Unmap(0, nullptr);

            mesh.vertexBufferView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
            mesh.vertexBufferView.StrideInBytes = sizeof(Vertex);
            mesh.vertexBufferView.SizeInBytes = vertexBufferSize;

        // --- インデックスバッファ ---
            resourceDesc.Width = indexBufferSize;
            if (!Check(m_device->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&mesh.indexBuffer))))
            {
                return false;
            }

            void* mappedIndex = nullptr;
            if (!Check(mesh.indexBuffer->Map(0, &readRange, &mappedIndex))) return false;
            memcpy(mappedIndex, prim.indices.data(), indexBufferSize);
            mesh.indexBuffer->Unmap(0, nullptr);

            mesh.indexBufferView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
            mesh.indexBufferView.SizeInBytes = indexBufferSize;
            mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

            gpuModel.meshes.push_back(std::move(mesh));
            hasMesh = true;
        }
    }

    // ※ 本来この規模のデータは DEFAULT ヒープへコピーすべき。
    //    UPLOAD ヒープのままだと GPU からの読み出しが遅い。
    //    正しさより手順の単純さを優先している箇所
    return hasMesh;
}

bool Renderer::UploadTexture(const void* pixels, UINT width, UINT height,
    DXGI_FORMAT format, ComPtr<ID3D12Resource>& outResource)
{
    // --- 1. ミップレベル数を決める ---
 // 長辺が 1 になるまで半分にできる回数 + 1
    UINT mipCount = 1;
    for (UINT size = (width > height ? width : height); size > 1; size >>= 1)
        ++mipCount;

    // baseColor や emissive は sRGB 格納。縮小時の平均を
    // リニア空間で行う必要があるので、フォーマットから判定する
    const bool isSrgb = (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    // --- 2. CPU 側でミップチェーンを作る ---
    // levels[0] が原寸、以降 1/2 ずつ
    std::vector<std::vector<uint8_t>> levels(mipCount);
    std::vector<UINT> levelWidth(mipCount), levelHeight(mipCount);

    levelWidth[0] = width;
    levelHeight[0] = height;
    levels[0].assign((const uint8_t*)pixels,
        (const uint8_t*)pixels + (size_t)width * height * 4);

    for (UINT mip = 1; mip < mipCount; ++mip)
    {
        const UINT srcW = levelWidth[mip - 1];
        const UINT srcH = levelHeight[mip - 1];
        const UINT dstW = (srcW > 1) ? srcW / 2 : 1;
        const UINT dstH = (srcH > 1) ? srcH / 2 : 1;

        levelWidth[mip] = dstW;
        levelHeight[mip] = dstH;
        levels[mip].resize((size_t)dstW * dstH * 4);

        const std::vector<uint8_t>& src = levels[mip - 1];
        std::vector<uint8_t>& dst = levels[mip];

        for (UINT y = 0; y < dstH; ++y)
        {
            // 元が 1 ピクセルしかない軸は、同じ行/列を 2 回読む
            const UINT y0 = (srcH > 1) ? y * 2 : 0;
            const UINT y1 = (srcH > 1) ? y * 2 + 1 : 0;

            for (UINT x = 0; x < dstW; ++x)
            {
                const UINT x0 = (srcW > 1) ? x * 2 : 0;
                const UINT x1 = (srcW > 1) ? x * 2 + 1 : 0;

                const size_t s00 = ((size_t)y0 * srcW + x0) * 4;
                const size_t s01 = ((size_t)y0 * srcW + x1) * 4;
                const size_t s10 = ((size_t)y1 * srcW + x0) * 4;
                const size_t s11 = ((size_t)y1 * srcW + x1) * 4;
                const size_t d = ((size_t)y * dstW + x) * 4;

                for (int c = 0; c < 4; ++c)
                {
                    // アルファ(c == 3)は常にリニア扱い
                    const bool convert = isSrgb && (c < 3);
                    const float* toLinear = SrgbToLinearTable();

                    float v0, v1, v2, v3;
                    if (convert)
                    {
                        // 表を引くだけ。powf は呼ばれない
                        v0 = toLinear[src[s00 + c]];
                        v1 = toLinear[src[s01 + c]];
                        v2 = toLinear[src[s10 + c]];
                        v3 = toLinear[src[s11 + c]];
                    }
                    else
                    {
                        v0 = src[s00 + c] / 255.0f;
                        v1 = src[s01 + c] / 255.0f;
                        v2 = src[s10 + c] / 255.0f;
                        v3 = src[s11 + c] / 255.0f;
                    }

                    const float avg = (v0 + v1 + v2 + v3) * 0.25f;
                    dst[d + c] = convert ? LinearToSrgbByte(avg)
                        : (uint8_t)(avg * 255.0f + 0.5f);
                }
            }
        }
    }

    // --- 3. テクスチャ本体(ミップ付き) ---
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = (UINT16)mipCount;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&outResource))))
    {
        return false;
    }

    // --- 4. 全ミップぶんの配置を一括で問い合わせる ---
    // 第 3 引数がサブリソース数。ミップ 1 枚ごとに 1 サブリソース
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipCount);
    std::vector<UINT>   numRows(mipCount);
    std::vector<UINT64> rowSizes(mipCount);
    UINT64 uploadSize = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, mipCount, 0,
        footprints.data(), numRows.data(),
        rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    if (!Check(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer))))
    {
        return false;
    }

    // --- 5. 中継バッファへ全ミップを書き込む ---
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (!Check(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
    {
        return false;
    }
    for (UINT mip = 0; mip < mipCount; ++mip)
    {
        for (UINT y = 0; y < numRows[mip]; ++y)
        {
            memcpy(mapped + footprints[mip].Offset
                + (SIZE_T)y * footprints[mip].Footprint.RowPitch,
                levels[mip].data() + (size_t)y * levelWidth[mip] * 4,
                (size_t)rowSizes[mip]);
        }
    }
    uploadBuffer->Unmap(0, nullptr);

    // --- 6. ミップごとにコピーコマンドを積む ---
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

    for (UINT mip = 0; mip < mipCount; ++mip)
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = outResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = mip;   // ミップ番号 = サブリソース番号

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprints[mip];

        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    return true;
}

bool Renderer::CreateMaterialTextures()
{
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    const uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };

    const UINT step = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    auto createSrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT slot)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = (UINT)-1;

            D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
            handle.ptr += (SIZE_T)slot * step;
            m_device->CreateShaderResourceView(resource, &srvDesc, handle);
        };

    for (auto& gpuModel : m_models)
    {
        const auto* model = gpuModel.model;

        // 画像添字から実体を引く。無効なら nullptr
        auto findImage = [&](int index) -> const GltfImage*
        {
            if (index < 0 || (size_t)index >= model->images.size()) return nullptr;
            const GltfImage& img = model->images[(size_t)index];
            return img.Valid() ? &img : nullptr;
        };

    // 同じ画像を複数のマテリアルが参照することがある。
    // 一度アップロードしたものは使い回して、VRAM と起動時間を節約する
        std::map<std::pair<int, DXGI_FORMAT>, ComPtr<ID3D12Resource>> cache;

    // テクスチャ 1 枚を用意して、指定スロットに SRV を作る
        auto loadInto = [&](int imageIndex, DXGI_FORMAT format,
            const uint8_t* fallback, UINT slot) -> bool
        {
            const GltfImage* img = findImage(imageIndex);

            // テクスチャが無い場合は 1x1 の無害な値。キャッシュ対象外
            if (img == nullptr)
            {
                ComPtr<ID3D12Resource> tex;
                if (!UploadTexture(fallback, 1, 1, format, tex)) return false;
                createSrv(tex.Get(), format, slot);
                gpuModel.materialTextures.push_back(tex);
                return true;
            }

            const auto key = std::make_pair(imageIndex, format);
            auto it = cache.find(key);
            if (it == cache.end())
            {
                ComPtr<ID3D12Resource> tex;
                if (!UploadTexture(img->pixels.data(), img->width, img->height, format, tex))
                    return false;
                gpuModel.materialTextures.push_back(tex);
                it = cache.emplace(key, tex).first;
            }
            createSrv(it->second.Get(), format, slot);
            return true;
        };

    // --- 各マテリアルのテクスチャ 5 枚を、5 個ずつ並べていく ---
        for (size_t i = 0; i < model->materials.size(); ++i)
        {
            const GltfMaterial& mat = model->materials[i];
            const UINT base = gpuModel.materialStart + (UINT)i * kTexturesPerMaterial;

        // 色として見えるものは sRGB、数値として使うものはリニア
            if (!loadInto(mat.baseColorImage, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                whitePixel, base + 0)) return false;
            if (!loadInto(mat.metallicRoughnessImage, DXGI_FORMAT_R8G8B8A8_UNORM,
                whitePixel, base + 1)) return false;
            if (!loadInto(mat.normalImage, DXGI_FORMAT_R8G8B8A8_UNORM,
                flatNormalPixel, base + 2)) return false;
            if (!loadInto(mat.occlusionImage, DXGI_FORMAT_R8G8B8A8_UNORM,
                whitePixel, base + 3)) return false;
            if (!loadInto(mat.emissiveImage, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                whitePixel, base + 4)) return false;
        }
    }

    // --- 既定マテリアル(床や、マテリアルを持たないプリミティブ用) ---
    if (!UploadTexture(whitePixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_defaultWhiteSrgb)) return false;
    if (!UploadTexture(whitePixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_defaultWhite))     return false;
    if (!UploadTexture(flatNormalPixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_defaultNormal))    return false;

    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvDefaultMaterial + 0);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 1);
    createSrv(m_defaultNormal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 2);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 3);
    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvDefaultMaterial + 4);

    GltfImage ground;
    if (!LoadTextureImage("assets/texture/terrain_ground.png", ground) ||
        !UploadTexture(ground.pixels.data(), ground.width, ground.height,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_floorTexture)) return false;
    createSrv(m_floorTexture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvTerrainMaterial);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvTerrainMaterial + 1);
    createSrv(m_defaultNormal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvTerrainMaterial + 2);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvTerrainMaterial + 3);
    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvTerrainMaterial + 4);

    GltfImage grass;
    if (!LoadTextureImage("assets/texture/grass_billboard.png", grass) ||
        !UploadTexture(grass.pixels.data(), grass.width, grass.height,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_grassTexture)) return false;
    createSrv(m_grassTexture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvGrassMaterial);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvGrassMaterial + 1);
    createSrv(m_defaultNormal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvGrassMaterial + 2);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvGrassMaterial + 3);
    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvGrassMaterial + 4);

    return true;
}

bool Renderer::CreateFloor()
{
    constexpr int stride = Terrain::Cells + 1;
    std::vector<Vertex> vertices(stride * stride);
    std::vector<uint32_t> indices;
    indices.reserve(Terrain::Cells * Terrain::Cells * 6);
    for (int z = 0; z < stride; ++z)
        for (int x = 0; x < stride; ++x)
        {
            auto& vertex = vertices[z * stride + x];
            const float px = x * Terrain::CellSize - Terrain::HalfSize;
            const float pz = z * Terrain::CellSize - Terrain::HalfSize;
            vertex.position = { px, Terrain::VertexHeight(x, z), pz };
            const float dx = (Terrain::VertexHeight(x + 1, z) - Terrain::VertexHeight(x - 1, z))
                / (2.0f * Terrain::CellSize);
            const float dz = (Terrain::VertexHeight(x, z + 1) - Terrain::VertexHeight(x, z - 1))
                / (2.0f * Terrain::CellSize);
            XMStoreFloat3(&vertex.normal, XMVector3Normalize(XMVectorSet(-dx, 1, -dz, 0)));
            XMStoreFloat4(&vertex.tangent, XMVector3Normalize(XMVectorSet(1, dx, 0, 0)));
            vertex.tangent.w = -1.0f; // UVの+Vはワールド+Z。
            vertex.uv = { px / 4.0f, pz / 4.0f };
        }
    for (int z = 0; z < Terrain::Cells; ++z)
        for (int x = 0; x < Terrain::Cells; ++x)
        {
            const uint32_t a = z * stride + x, b = a + stride;
            indices.insert(indices.end(), { a, b, b + 1, a, b + 1, a + 1 });
        }

    const auto createMeshBuffers = [&](const std::vector<Vertex>& sourceVertices,
        const std::vector<uint32_t>& sourceIndices,
        ComPtr<ID3D12Resource>& vertexBuffer, ComPtr<ID3D12Resource>& indexBuffer,
        D3D12_VERTEX_BUFFER_VIEW& vertexView, D3D12_INDEX_BUFFER_VIEW& indexView)
    {
        const UINT vertexBufferSize = static_cast<UINT>(
            sourceVertices.size() * sizeof(Vertex));
        const UINT indexBufferSize = static_cast<UINT>(
            sourceIndices.size() * sizeof(uint32_t));
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = vertexBufferSize;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (!Check(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
            &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&vertexBuffer)))) return false;
        D3D12_RANGE readRange = { 0, 0 };
        void* mapped = nullptr;
        if (!Check(vertexBuffer->Map(0, &readRange, &mapped))) return false;
        memcpy(mapped, sourceVertices.data(), vertexBufferSize);
        vertexBuffer->Unmap(0, nullptr);
        vertexView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
        vertexView.StrideInBytes = sizeof(Vertex);
        vertexView.SizeInBytes = vertexBufferSize;
        resourceDesc.Width = indexBufferSize;
        if (!Check(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
            &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&indexBuffer)))) return false;
        void* mappedIndex = nullptr;
        if (!Check(indexBuffer->Map(0, &readRange, &mappedIndex))) return false;
        memcpy(mappedIndex, sourceIndices.data(), indexBufferSize);
        indexBuffer->Unmap(0, nullptr);
        indexView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
        indexView.SizeInBytes = indexBufferSize;
        indexView.Format = DXGI_FORMAT_R32_UINT;
        return true;
    };

    m_floorIndexCount = static_cast<UINT>(indices.size());
    if (!createMeshBuffers(vertices, indices, m_floorVertexBuffer, m_floorIndexBuffer,
        m_floorVertexBufferView, m_floorIndexBufferView)) return false;

    std::vector<Vertex> grassVertices;
    std::vector<uint32_t> grassIndices;
    const auto addBlade = [&](float centerX, float centerZ, float angle,
        float height, float width)
    {
        const float dx = std::cos(angle) * width * 0.5f;
        const float dz = std::sin(angle) * width * 0.5f;
        const float y = Terrain::HeightAt(centerX, centerZ) + 0.02f;
        const XMFLOAT3 normal { -std::sin(angle), 0.0f, std::cos(angle) };
        const uint32_t base = static_cast<uint32_t>(grassVertices.size());
        Vertex blade[6] = {};
        for (int level = 0; level < 3; ++level)
        {
            const float bendWeight = float(level) * 0.5f;
            for (int side = 0; side < 2; ++side)
            {
                const int i = level * 2 + side;
                const float sideSign = side == 0 ? -1.0f : 1.0f;
                blade[i].position = { centerX + dx * sideSign, y + height * bendWeight,
                    centerZ + dz * sideSign };
                blade[i].normal = normal;
                blade[i].tangent = { std::cos(angle), 0.0f, std::sin(angle), 1.0f };
                // 画像中央の草1本だけを使い、左右の透明余白は拡大しない。
                blade[i].uv = { side == 0 ? 0.45f : 0.55f, 1.0f - bendWeight };
                // 根元=0、中腹=0.5、葉先=1。風・接触変形の曲げ係数。
                blade[i].weights.x = bendWeight;
                grassVertices.push_back(blade[i]);
            }
        }
        // 描画PSOは両面表示なので、裏面用の重複三角形は作らない。
        grassIndices.insert(grassIndices.end(), {
            base, base + 1, base + 3, base, base + 3, base + 2,
            base + 2, base + 3, base + 5, base + 2, base + 5, base + 4
        });
    };

    constexpr float grassRadius = Terrain::WalkableRadius + 4.0f;
    constexpr float spacing = 0.24f;
    for (int gz = 0; gz <= static_cast<int>(grassRadius * 2.0f / spacing); ++gz)
    {
        for (int gx = 0; gx <= static_cast<int>(grassRadius * 2.0f / spacing); ++gx)
        {
            uint32_t hash = static_cast<uint32_t>(gx * 73856093u) ^
                static_cast<uint32_t>(gz * 19349663u);
            hash ^= hash >> 13;
            if (hash % 100u >= 92u) continue;
            const float jitterX = (float((hash >> 8) & 255u) / 255.0f - 0.5f) * 0.20f;
            const float jitterZ = (float((hash >> 16) & 255u) / 255.0f - 0.5f) * 0.20f;
            const float x = -grassRadius + gx * spacing + jitterX;
            const float z = -grassRadius + gz * spacing + jitterZ;
            if (x * x + z * z > grassRadius * grassRadius)
                continue;
            const float pondX = x - Terrain::PondCenterX;
            const float pondZ = z - Terrain::PondCenterZ;
            const float pondClearRadius = Terrain::PondRadius + 0.3f;
            if (pondX * pondX + pondZ * pondZ < pondClearRadius * pondClearRadius)
                continue;
            const float angle = XM_2PI * float(hash & 1023u) / 1024.0f;
            const float height = 0.40f + 0.18f * float((hash >> 20) & 255u) / 255.0f;
            const float width = 0.06f +
                0.03f * float((hash >> 12) & 255u) / 255.0f;
            addBlade(x, z, angle, height, width);
        }
    }
    m_grassIndexCount = static_cast<UINT>(grassIndices.size());
    if (grassVertices.empty() || !createMeshBuffers(grassVertices, grassIndices,
        m_grassVertexBuffer, m_grassIndexBuffer,
        m_grassVertexBufferView, m_grassIndexBufferView)) return false;

    // 水面は十分に分割した独立メッシュ。後から頂点シェーダで波を加えられる。
    constexpr uint32_t waterSegments = 128;
    constexpr uint32_t waterRings = 24;
    std::vector<Vertex> waterVertices(1 + waterSegments * waterRings);
    std::vector<uint32_t> waterIndices;
    waterIndices.reserve(waterSegments * (3 + (waterRings - 1) * 6));
    const auto setWaterVertex = [](Vertex& vertex, float x, float z)
    {
        vertex.position = { x, Terrain::PondWaterHeight, z };
        vertex.normal = { 0.0f, 1.0f, 0.0f };
        vertex.tangent = { 1.0f, 0.0f, 0.0f, -1.0f };
        vertex.uv = {
            0.5f + (x - Terrain::PondCenterX) / (Terrain::PondWaterRadius * 2.0f),
            0.5f + (z - Terrain::PondCenterZ) / (Terrain::PondWaterRadius * 2.0f)
        };
    };
    setWaterVertex(waterVertices[0], Terrain::PondCenterX, Terrain::PondCenterZ);
    for (uint32_t ring = 1; ring <= waterRings; ++ring)
    {
        const float radius = Terrain::PondWaterRadius * float(ring) / float(waterRings);
        for (uint32_t segment = 0; segment < waterSegments; ++segment)
        {
            const float angle = XM_2PI * float(segment) / float(waterSegments);
            setWaterVertex(waterVertices[1 + (ring - 1) * waterSegments + segment],
                Terrain::PondCenterX + std::cos(angle) * radius,
                Terrain::PondCenterZ + std::sin(angle) * radius);
        }
    }
    for (uint32_t segment = 0; segment < waterSegments; ++segment)
    {
        const uint32_t current = 1 + segment;
        const uint32_t next = 1 + (segment + 1) % waterSegments;
        waterIndices.insert(waterIndices.end(), { 0, next, current });
    }
    for (uint32_t ring = 2; ring <= waterRings; ++ring)
    {
        const uint32_t inner = 1 + (ring - 2) * waterSegments;
        const uint32_t outer = 1 + (ring - 1) * waterSegments;
        for (uint32_t segment = 0; segment < waterSegments; ++segment)
        {
            const uint32_t next = (segment + 1) % waterSegments;
            waterIndices.insert(waterIndices.end(), {
                inner + segment, inner + next, outer + next,
                inner + segment, outer + next, outer + segment
            });
        }
    }
    m_waterIndexCount = static_cast<UINT>(waterIndices.size());
    return createMeshBuffers(waterVertices, waterIndices,
        m_waterVertexBuffer, m_waterIndexBuffer,
        m_waterVertexBufferView, m_waterIndexBufferView);
}

bool Renderer::CreateConstantBuffer()
{
    // 1 オブジェクトぶんのサイズ。256 バイト境界に揃える必要がある
    m_cbStride = (sizeof(SceneConstants) + 255) & ~255u;

    // メッシュ群 + 地形 + 草 + 水面の領域をフレーム数ぶん確保する。
    m_objectCount = kMaxDrawMeshCount + 3;
    const UINT bufferSize = m_cbStride * m_objectCount * kFrameCount;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (!Check(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffer))))
    {
        return false;
    }

    // Map したポインタを Unmap せずに保持する。
    // 毎フレーム memcpy するだけで GPU に値が届く。
    D3D12_RANGE readRange = { 0, 0 };   // CPU からは読まない宣言
    return Check(m_constantBuffer->Map(0, &readRange, &m_cbMapped));
}
