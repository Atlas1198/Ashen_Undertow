#include "RendererInternal.h"

#include <algorithm>

using namespace DirectX;
using namespace RendererDetail;

void Renderer::DrawSceneMeshes(bool shadowPass, D3D12_GPU_VIRTUAL_ADDRESS constantBase)
{
    const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const auto heapStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    size_t constantIndex = 0;
    for (const auto& command : m_drawCommands)
    {
        if (command.modelIndex >= m_models.size()) continue;
        const auto& model = m_models[command.modelIndex];
        for (const auto& mesh : model.meshes)
        {
            if (!shadowPass)
                m_commandList->SetPipelineState(mesh.doubleSided
                    ? m_pipelineStateNoCull.Get() : m_pipelineState.Get());
            m_commandList->SetGraphicsRootConstantBufferView(
                0, constantBase + constantIndex++ * m_cbStride);
            const UINT slot = mesh.material >= 0
                ? model.materialStart + UINT(mesh.material) * kTexturesPerMaterial
                : kSrvDefaultMaterial;
            auto material = heapStart;
            material.ptr += SIZE_T(slot) * srvStep;
            m_commandList->SetGraphicsRootDescriptorTable(2, material);
            m_commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
            m_commandList->IASetIndexBuffer(&mesh.indexBufferView);
            m_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
        }
    }
}

void Renderer::DrawModel(ModelHandle modelHandle, const XMFLOAT4X4& world,
    const std::vector<XMFLOAT4X4>* skinMatrices, const XMFLOAT4X4* shadowWorld,
    uint32_t renderFlags, float dissolveAmount, float dissolveBaseY,
    const XMFLOAT4& rimLightColor)
{
    for (uint32_t i = 0; i < m_models.size(); ++i)
    {
        const auto& model = m_models[i];
        if (model.handle.value != modelHandle.value) continue;
        if (m_drawMeshCount + model.meshes.size() > kMaxDrawMeshCount) return;

        DrawCommand command { i, world, shadowWorld ? *shadowWorld : world,
            renderFlags, dissolveAmount, dissolveBaseY, rimLightColor };
        if (skinMatrices)
            command.skinMatrices = *skinMatrices;
        m_drawCommands.push_back(std::move(command));
        m_drawMeshCount += model.meshes.size();
        return;
    }
}

void Renderer::Render(const UIManager& ui)
{
    if (!PrepareUIImages(ui))
    {
        OutputDebugStringA("UI texture upload failed.\n");
        m_drawCommands.clear();
        m_drawMeshCount = 0;
        m_debugColliders.clear();
        m_effectParticles.clear();
        m_alphaEffectParticles.clear();
        m_waterRipples.clear();
        return;
    }
    // --- 定数バッファの更新 ---
    // 床のワールド行列。各オブジェクトの行列は描画要求から取得する。
    XMMATRIX world = XMMatrixIdentity();

    // Cameraが計算したワールド座標からView行列を作る。
    XMVECTOR target = XMVectorSetW(XMLoadFloat3(&m_cameraTarget), 1.0f);
    XMVECTOR eye = XMVectorSetW(XMLoadFloat3(&m_cameraPosition), 1.0f);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);  // 上方向
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    // Projection 行列: 透視投影。
    //   fovY   : 垂直画角。60度くらいが自然。広げると魚眼寄りになる
    //   aspect : 画面の縦横比。これが歪みを補正している本体
    //   near/far: 描画する深度の範囲。near を小さくし過ぎると
    //             深度バッファの精度が落ちる(Zファイティングの原因)
    const float fovY = XMConvertToRadians(60.0f);
    const float aspect = (float)m_width / (float)m_height;
    // near を大きくすると深度バッファの精度が上がる。
    // 0.1 → 0.5 にするだけで、遠くの壁の Z ファイティングが目に見えて減る
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, aspect, 0.5f, 100.0f);

    // DirectXMath は行ベクトル規約なので、適用したい順に左から掛ける
    XMMATRIX mvp = world * view * proj;

    // 同じバックバッファのフェンス待機後に、このフレーム専用領域を再利用する。
    const size_t frameOffset = size_t(m_frameIndex) * m_objectCount * m_cbStride;
    auto* frameConstants = static_cast<uint8_t*>(m_cbMapped) + frameOffset;
    const auto frameCbBase = m_constantBuffer->GetGPUVirtualAddress() + frameOffset;
    SceneConstants cb {};
    XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(mvp));    // HLSL は列優先なので転置
    XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));  // 法線変換用
    static const auto windStart = std::chrono::steady_clock::now();
    cb.windTime = std::chrono::duration<float>(
        std::chrono::steady_clock::now() - windStart).count();
    // 風筋が前後へ反転して見えないよう、風向は常に一方向へ固定する。
    cb.windDirection = { 0.85f, 0.0f, 0.53f };
    cb.windStrength = 1.15f + 0.18f * std::sin(cb.windTime * 0.83f)
        + 0.10f * std::sin(cb.windTime * 1.71f + 0.8f);
    cb.grassInteractors[0] = m_grassInteractors[0];
    cb.grassInteractors[1] = m_grassInteractors[1];
    cb.waterInteractionStrength = m_waterInteractionStrength;
    for (size_t i = 0; i < m_waterRipples.size() && i < kMaxWaterRipples; ++i)
        cb.waterRipples[i] = m_waterRipples[i];

    // ビュー射影行列の逆行列。
    // スカイボックスが各ピクセルの視線方向を復元するのに使う
    XMVECTOR det;
    XMMATRIX invViewProj = XMMatrixInverse(&det, view * proj);
    XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(invViewProj));


    // カメラ位置。鏡面反射は「どこから見ているか」で見え方が変わるので必須
    XMStoreFloat3(&cb.cameraPosition, eye);

    // 平行光源(太陽のようなもの)。位置を持たず「向き」だけを持つ
    // env.hdr の太陽中心(UV 0.60083, 0.33838)から求めた光の進行方向。
    // IBLの太陽と直接光のハイライト・影を同じ方向へ揃える。
    XMVECTOR lightDir = XMVector3Normalize(
        XMVectorSet(-0.704264f, -0.486210f, -0.517311f, 0.0f));

    // --- ライト視点のビュー射影行列(シャドウマップ用) ---
    // 平行光源は「無限遠から平行に降る光」なので、射影は直交投影を使う。
    // 透視投影と違い、距離によって影の大きさが変わらない
    const XMVECTOR sceneCenter = XMVectorSet(0.0f, 5.0f, 0.0f, 1.0f);
    const XMVECTOR lightEye =
        XMVectorSubtract(sceneCenter, XMVectorScale(lightDir, 75.0f));

    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, sceneCenter,
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    // 地形と外周の丘を覆う範囲。
    // 広げるほど 1 テクセルが担当する面積が増えて影が粗くなるので、
    // 「全部入る最小の広さ」を狙うのが定石
    XMMATRIX lightProj = XMMatrixOrthographicLH(110.0f, 110.0f, 0.1f, 180.0f);
    XMMATRIX lightViewProj = lightView * lightProj;

    XMStoreFloat3(&cb.lightDirection, lightDir);

    // PBR では拡散項を 1/PI で正規化するため、ライトの強さは 1.0 では暗すぎる。
    // 「明るさ = 色 × 強度」として分けて考えるのが扱いやすい
    const float lightIntensity = 3.0f;
    cb.lightColor = { 1.00f * lightIntensity,
                      0.97f * lightIntensity,
                      0.92f * lightIntensity };

    // 環境光。次のレッスンで IBL に置き換わる仮の値
    cb.ambientColor = { 0.10f, 0.11f, 0.14f };

    XMStoreFloat4x4(&cb.lightViewProj, XMMatrixTranspose(lightViewProj));

    // --- DrawModel で登録された各モデルインスタンスの定数を書く ---
    size_t drawConstantIndex = 0;
    for (const DrawCommand& command : m_drawCommands)
    {
        if (command.modelIndex >= m_models.size()) continue;
        const auto& gpuModel = m_models[command.modelIndex];
        const auto& model = *gpuModel.model;
        const XMMATRIX objectWorld = XMLoadFloat4x4(&command.world);
        const XMMATRIX objectShadowWorld = XMLoadFloat4x4(&command.shadowWorld);
        XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(objectWorld * view * proj));
        XMStoreFloat4x4(&cb.world, XMMatrixTranspose(objectWorld));
        XMStoreFloat4x4(&cb.shadowWorld, XMMatrixTranspose(objectShadowWorld));
        cb.jointCount = static_cast<uint32_t>(std::min<size_t>(
            command.skinMatrices.size(), GltfMaxSkinJoints));
        for (uint32_t joint = 0; joint < cb.jointCount; ++joint)
        {
            XMStoreFloat4x4(&cb.jointMatrices[joint], XMMatrixTranspose(
                XMLoadFloat4x4(&command.skinMatrices[joint])));
        }

        for (size_t i = 0; i < gpuModel.meshes.size(); ++i)
        {
            const int matIndex = gpuModel.meshes[i].material;
            if (matIndex >= 0 && (size_t)matIndex < model.materials.size())
            {
                const GltfMaterial& mat = model.materials[(size_t)matIndex];
                cb.albedo = { mat.baseColorFactor[0], mat.baseColorFactor[1],
                    mat.baseColorFactor[2] };
                cb.metallic = mat.metallicFactor;
                cb.roughness = mat.roughnessFactor;
                const float kEmissiveStrength = 4.0f;
                cb.emissiveFactor = { mat.emissiveFactor[0] * kEmissiveStrength,
                    mat.emissiveFactor[1] * kEmissiveStrength,
                    mat.emissiveFactor[2] * kEmissiveStrength };
            }
            else
            {
                cb.albedo = { 1.0f, 1.0f, 1.0f };
                cb.metallic = 0.0f;
                cb.roughness = 0.5f;
                cb.emissiveFactor = { 0.0f, 0.0f, 0.0f };
            }

            cb.alphaCutoff = gpuModel.meshes[i].alphaCutoff;
            cb.materialOpacity = 1.0f;
            cb.renderFlags = command.renderFlags;
            cb.dissolveAmount = command.dissolveAmount;
            cb.dissolveBaseY = command.dissolveBaseY;
            cb.rimLightColor = command.rimLightColor;
            memcpy(frameConstants + drawConstantIndex * m_cbStride,
                &cb, sizeof(cb));
            ++drawConstantIndex;
        }
    }

    // --- 床(固定の最終スロット) ---
    const size_t floorConstantIndex = kMaxDrawMeshCount;
    XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(world * view * proj));
    XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));
    XMStoreFloat4x4(&cb.shadowWorld, XMMatrixTranspose(world));
    cb.albedo = { 1.0f, 1.0f, 1.0f };
    cb.alphaCutoff = -1.0f;
    cb.metallic = 0.0f;
    cb.roughness = 0.95f;
    cb.emissiveFactor = { 0.0f, 0.0f, 0.0f };
    cb.jointCount = 0;
    cb.materialOpacity = 1.0f;
    cb.renderFlags = 4;
    cb.dissolveAmount = 0.0f;
    cb.dissolveBaseY = 0.0f;
    cb.rimLightColor = {};
    memcpy(frameConstants + floorConstantIndex * m_cbStride, &cb, sizeof(cb));

    // 草は画像の透明部分を捨て、草葉の輪郭だけを描画する。
    const size_t grassConstantIndex = kMaxDrawMeshCount + 1;
    cb.alphaCutoff = 0.45f;
    cb.roughness = 0.9f;
    cb.renderFlags = 1;
    memcpy(frameConstants + grassConstantIndex * m_cbStride, &cb, sizeof(cb));

    const size_t waterConstantIndex = kMaxDrawMeshCount + 2;
    cb.albedo = { 0.035f, 0.24f, 0.31f };
    cb.alphaCutoff = -1.0f;
    cb.metallic = 0.0f;
    cb.roughness = 0.08f;
    cb.materialOpacity = 0.58f;
    cb.renderFlags = 2;
    memcpy(frameConstants + waterConstantIndex * m_cbStride, &cb, sizeof(cb));

    // 1. このフレーム用のアロケータを Reset。
    //    (WaitForGpu 済み or フェンスで安全が確認できているときだけ許される)
    ID3D12CommandAllocator* allocator = m_commandAllocators[m_frameIndex].Get();
    allocator->Reset();
    m_commandList->Reset(allocator, m_pipelineState.Get());

    // ===== シャドウマップ生成パス =====
// ライト視点から深度だけを描く。本描画より前に済ませておく必要がある
    {
        D3D12_RESOURCE_BARRIER shadowBarrier = {};
        shadowBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        shadowBarrier.Transition.pResource = m_shadowMap.Get();
        shadowBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        m_commandList->ResourceBarrier(1, &shadowBarrier);

        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv =
            m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        // カラーの出力先は無し。深度だけを描くのでレンダーターゲットは 0 個
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
        m_commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH,
            1.0f, 0, 0, nullptr);

        // ビューポートは画面ではなくシャドウマップの解像度
        D3D12_VIEWPORT shadowVp =
        { 0.0f, 0.0f, (float)kShadowMapSize, (float)kShadowMapSize, 0.0f, 1.0f };
        D3D12_RECT shadowSc = { 0, 0, (LONG)kShadowMapSize, (LONG)kShadowMapSize };
        m_commandList->RSSetViewports(1, &shadowVp);
        m_commandList->RSSetScissorRects(1, &shadowSc);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        // ディスクリプタテーブルを使う前に、必ずヒープをバインドする。
        // これはコマンドリスト単位の状態で、ルートシグネチャとは独立している
        ID3D12DescriptorHeap* shadowHeaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(shadowHeaps), shadowHeaps);

        m_commandList->SetPipelineState(m_shadowPSO.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        DrawSceneMeshes(true, frameCbBase);

        // 起伏のある地形自身も影を落とす。
        m_commandList->SetGraphicsRootConstantBufferView(0,
            frameCbBase + floorConstantIndex * m_cbStride);
        auto terrainSrv = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        terrainSrv.ptr += SIZE_T(kSrvTerrainMaterial) * m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_commandList->SetGraphicsRootDescriptorTable(2, terrainSrv);
        m_commandList->IASetVertexBuffers(0, 1, &m_floorVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_floorIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_floorIndexCount, 1, 0, 0, 0);
        m_commandList->SetGraphicsRootConstantBufferView(0,
            frameCbBase + grassConstantIndex * m_cbStride);
        auto grassSrv = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        grassSrv.ptr += SIZE_T(kSrvGrassMaterial) * m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_commandList->SetGraphicsRootDescriptorTable(2, grassSrv);
        m_commandList->IASetVertexBuffers(0, 1, &m_grassVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_grassIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_grassIndexCount, 1, 0, 0, 0);

        // 深度書き込み → テクスチャとして読める状態へ戻す
        shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &shadowBarrier);
    }

    // 2. HDR バッファを描画先にする。
    //    バックバッファ(8bit)に直接描くと 1.0 を超える明るさがその場で
    //    失われてしまうため、シーンは 16bit 浮動小数のバッファへ描く
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.pResource = m_hdrBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    // 3. レンダーターゲット設定 + クリア
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv =
        m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsvHandle);

    // スカイボックスが全画面を塗るので色のクリアは本来不要だが、
    // 未初期化のメモリを読む事故を防ぐために入れておく
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_commandList->ClearRenderTargetView(hdrRtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    // 4. ビューポート/シザー(PSO には含まれないので毎フレーム設定)
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissor      = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    // --- 背景(スカイボックス)を先に描く ---
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // シェーダから見えるヒープをバインドする。
    // これはコマンドリスト単位の状態なので、描画ごとに設定し直す必要はない
    ID3D12DescriptorHeap* shadowHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(shadowHeaps), shadowHeaps);

    m_commandList->SetGraphicsRootConstantBufferView(
        0, frameCbBase + floorConstantIndex * m_cbStride);
    // ルートパラメータ 1 番に、SRV が置いてあるヒープ上の位置を渡す
    m_commandList->SetGraphicsRootDescriptorTable(
        1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // スカイボックスは t4〜t8 を使わないが、
    // ルートパラメータは全部設定しておくのが安全
    D3D12_GPU_DESCRIPTOR_HANDLE skyMaterial = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    skyMaterial.ptr += kSrvDefaultMaterial * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_commandList->SetGraphicsRootDescriptorTable(2, skyMaterial);

    // ルートパラメータ 3 番にシャドウマップ(ヒープ 14 番)
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv =
        m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    shadowSrv.ptr += kSrvShadowMap * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_commandList->SetGraphicsRootDescriptorTable(3, shadowSrv);

    m_commandList->SetPipelineState(m_skyboxPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 頂点バッファなしで 3 頂点。シェーダが SV_VertexID から座標を作る
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // --- ここからモデル ---
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_GPU_DESCRIPTOR_HANDLE heapStart =
        m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    const D3D12_GPU_VIRTUAL_ADDRESS cbBase =
        frameCbBase;

    DrawSceneMeshes(false, frameCbBase);

    // --- 床（モデル群の次の定数スロット、標準マテリアル） ---
    {
        m_commandList->SetPipelineState(m_pipelineState.Get());
        m_commandList->SetGraphicsRootConstantBufferView(
            0, cbBase + floorConstantIndex * m_cbStride);

        D3D12_GPU_DESCRIPTOR_HANDLE material = heapStart;
        material.ptr += kSrvTerrainMaterial * srvStep;
        m_commandList->SetGraphicsRootDescriptorTable(2, material);

        m_commandList->IASetVertexBuffers(0, 1, &m_floorVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_floorIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_floorIndexCount, 1, 0, 0, 0);

        material = heapStart;
        material.ptr += kSrvGrassMaterial * srvStep;
        m_commandList->SetGraphicsRootDescriptorTable(2, material);
        m_commandList->SetGraphicsRootConstantBufferView(
            0, cbBase + grassConstantIndex * m_cbStride);
        m_commandList->SetPipelineState(m_pipelineStateNoCull.Get());
        m_commandList->IASetVertexBuffers(0, 1, &m_grassVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_grassIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_grassIndexCount, 1, 0, 0, 0);

        // 半透明物は不透明物の後に描く。
        material = heapStart;
        material.ptr += kSrvDefaultMaterial * srvStep;
        m_commandList->SetGraphicsRootDescriptorTable(2, material);
        m_commandList->SetGraphicsRootConstantBufferView(
            0, cbBase + waterConstantIndex * m_cbStride);
        m_commandList->SetPipelineState(m_waterPipelineState.Get());
        m_commandList->IASetVertexBuffers(0, 1, &m_waterVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_waterIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_waterIndexCount, 1, 0, 0, 0);

        // 風筋は頂点バッファを持たず、GPU上で64本のリボンを生成・移動する。
        m_commandList->SetPipelineState(m_windPipelineState.Get());
        m_commandList->SetGraphicsRootConstantBufferView(
            0, cbBase + floorConstantIndex * m_cbStride);
        m_commandList->IASetVertexBuffers(0, 0, nullptr);
        m_commandList->IASetIndexBuffer(nullptr);
        m_commandList->DrawInstanced(8 * 6, 64, 0, 0);
    }

    DrawEffects(view * proj);
    DrawColliderDebug(view * proj);

    // ===== ブルーム =====
    {
        // HDR バッファ: 描画先 → テクスチャとして読める状態へ
        barrier.Transition.pResource = m_hdrBuffer.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &barrier);

        const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT rtvStep = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto srvOf = [&](UINT index)
            {
                D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
                h.ptr += (SIZE_T)index * srvStep;
                return h;
            };
        auto bloomRtvOf = [&](UINT index)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE h = m_bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
                h.ptr += (SIZE_T)index * rtvStep;
                return h;
            };
        // A と B の状態を入れ替えるバリアを短く書くためのヘルパ
        auto transition = [&](ID3D12Resource* res,
            D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
            {
                barrier.Transition.pResource = res;
                barrier.Transition.StateBefore = from;
                barrier.Transition.StateAfter = to;
                m_commandList->ResourceBarrier(1, &barrier);
            };

        const D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES kWrite = D3D12_RESOURCE_STATE_RENDER_TARGET;

        ID3D12DescriptorHeap* postProcessHeaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(1, postProcessHeaps);
        m_commandList->SetGraphicsRootSignature(m_bloomRootSignature.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ビューポートは 1/4 解像度
        D3D12_VIEWPORT bloomVp =
        { 0.0f, 0.0f, (float)m_bloomWidth, (float)m_bloomHeight, 0.0f, 1.0f };
        D3D12_RECT bloomSc = { 0, 0, (LONG)m_bloomWidth, (LONG)m_bloomHeight };
        m_commandList->RSSetViewports(1, &bloomVp);
        m_commandList->RSSetScissorRects(1, &bloomSc);

        // --- 1. 輝度抽出: HDR(フル解像度) → A(1/4 解像度) ---
        transition(m_bloomA.Get(), kRead, kWrite);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvA = bloomRtvOf(0);
        m_commandList->OMSetRenderTargets(1, &rtvA, FALSE, nullptr);

        m_commandList->SetPipelineState(m_bloomBrightPSO.Get());
        const float brightParams[4] =
        {
            m_bloomThreshold, 0.0f,
            1.0f / (float)m_width, 1.0f / (float)m_height   // 入力のテクセルサイズ
        };
        m_commandList->SetGraphicsRoot32BitConstants(0, 4, brightParams, 0);
        m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvHdrBuffer));   // HDR バッファ
        m_commandList->DrawInstanced(3, 1, 0, 0);

        // --- 2. 横 → 縦 のぼかしを 2 往復 ---
        // 回数を増やすほど滲みが広がる
        m_commandList->SetPipelineState(m_bloomBlurPSO.Get());

        const float texelX = 1.0f / (float)m_bloomWidth;
        const float texelY = 1.0f / (float)m_bloomHeight;

        for (int i = 0; i < 2; ++i)
        {
            // 横方向: A → B
            transition(m_bloomA.Get(), kWrite, kRead);
            transition(m_bloomB.Get(), kRead, kWrite);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvB = bloomRtvOf(1);
            m_commandList->OMSetRenderTargets(1, &rtvB, FALSE, nullptr);
            const float horizontal[4] = { texelX, 0.0f, 0.0f, 0.0f };
            m_commandList->SetGraphicsRoot32BitConstants(0, 4, horizontal, 0);
            m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvBloomA));   // A
            m_commandList->DrawInstanced(3, 1, 0, 0);

            // 縦方向: B → A
            transition(m_bloomB.Get(), kWrite, kRead);
            transition(m_bloomA.Get(), kRead, kWrite);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvA2 = bloomRtvOf(0);
            m_commandList->OMSetRenderTargets(1, &rtvA2, FALSE, nullptr);
            const float vertical[4] = { 0.0f, texelY, 0.0f, 0.0f };
            m_commandList->SetGraphicsRoot32BitConstants(0, 4, vertical, 0);
            m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvBloomB));   // B
            m_commandList->DrawInstanced(3, 1, 0, 0);
        }

        // 最終結果は A にある。読める状態に戻す
        transition(m_bloomA.Get(), kWrite, kRead);
    }

    // ===== 6. トーンマップパス(HDR バッファ → バックバッファ) =====
    {
        // バックバッファ: 表示用 → 描画先へ
        barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += (SIZE_T)m_frameIndex * m_rtvDescriptorSize;
        // 全画面を塗り替えるので、深度もクリアも不要
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // ビューポートはコマンドリストの状態として残り続けるので、
        // ブルームで 1/4 解像度に変えたぶんを画面サイズへ戻す
        D3D12_VIEWPORT fullVp =
        { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
        D3D12_RECT fullSc = { 0, 0, (LONG)m_width, (LONG)m_height };
        m_commandList->RSSetViewports(1, &fullVp);
        m_commandList->RSSetScissorRects(1, &fullSc);

        m_commandList->SetGraphicsRootSignature(m_tonemapRootSignature.Get());
        m_commandList->SetPipelineState(m_tonemapPSO.Get());

        // 露出をルート定数で渡す
        const float tonemapParams[2] = { m_exposure, m_bloomIntensity };
        m_commandList->SetGraphicsRoot32BitConstants(0, 2, tonemapParams, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE hdrSrv =
            m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        hdrSrv.ptr += kSrvHdrBuffer * m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_commandList->SetGraphicsRootDescriptorTable(1, hdrSrv);

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        DrawUI(ui);

        // バックバッファ: 描画先 → 表示用へ
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_commandList->ResourceBarrier(1, &barrier);
    }

    // 7. 記録終了 → キューへ投入
    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // 8. 表示(垂直同期あり)
    m_swapChain->Present(1, 0);

    // 9. フレーム同期:
    //    今投げたフレームの完了目標値をキューに Signal し、
    //    「次に使うバックバッファ」のフェンス値がまだなら待つ。
    //    (2 枚バッファなので、実質 1 フレーム前の完了を待つことになる)
    const uint64_t currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
    m_drawCommands.clear();
    m_drawMeshCount = 0;
    m_debugColliders.clear();
    m_effectParticles.clear();
    m_alphaEffectParticles.clear();
    m_waterRipples.clear();
    m_effectTextureIndex = UINT32_MAX;
}
