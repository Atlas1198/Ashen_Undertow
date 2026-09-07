# Handoff

## 方針
- 個人制作。`intent.md` / `plan.md` は使わない。
- 必要なファイルだけ読む。大きなファイルは関係箇所だけ読む。
- `main` からゲーム側で直接呼ぶのは `Manager` のみ。
- 実装は小さく進め、変更後にビルド・回帰テストする。

## 現在の構成
- `src/Engine/`: エンジン共通コード。Visual Studioでも「ソース ファイル/Engine」「ヘッダー ファイル/Engine」に分類済み。
- ゲーム側: `main.*`, `Scene_Game.*`, `Scene_Title.*`, `Player.*`, `Enemy.*` は `src/` 直下。
- `Manager`: `Input` → `SceneManager` → `Renderer` の実行と寿命管理。
- `SceneManager`: `ObjectManager` を所有。更新順は `OnUpdate` → Object → Component → Collision → `OnAfterUpdate`。その後描画。
- `ObjectManager::AddGameObject<T>()`: 生成・`Initialize`・所有・Update・Drawを自動管理。更新中に追加したObjectは次フレームから更新。
- `GameObject`: Transformと`ComponentManager`を所有。
- `ModelComponent`: モデル読込と個別World行列の描画。
- `ModelManager`: 正規化パス単位のCPUモデルキャッシュ。glTF/GLB対応、FBX未対応。
- `Renderer`: DX12。実装は `RendererDevice/Models/Environment/Pipelines/PostProcess/Frame.cpp` に分割。GPU登録モデルは現在1種類、複数Instanceは可能。定数バッファはバックバッファ別領域。

## 実装済みゲーム機能
- `Scene_Game` が `Player` と `Enemy` 1体を登録。
- Player: WASD移動、`assets/model.glb`表示。
- Camera: Player追従、常時マウス視点操作、左右反転。Escでカーソル表示、左クリックで操作再開。Escは終了に使わない。
- Collider: Sphere / Y軸Capsule / AABB Box。全組合せの接触検出。
- 接触は`Collider::GetContacts()`。押し戻し・ダメージ・イベントは未実装。Collider寸法はTransformの回転・拡縮と独立。

## 検証
```powershell
powershell -ExecutionPolicy Bypass -File tests/RunTests.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RenderEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```
- 最終確認: Debug/Releaseビルド成功。ComponentManager/Collision/Engineテスト成功。
- ウィンドウ上の見た目・操作は最終変更後未確認。

## 作業状態
- リファクタリング、Collider追加、テスト追加を含む未コミット変更あり。勝手に破棄しない。
- `.claude/` は既存の未追跡物。変更しない。
- 次の機能は未決定。新チャットでは本ファイルを先に読み、依頼に関係するファイルだけ追加確認する。
