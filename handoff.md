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
- `AnimationComponent`: glTFのスキニングアニメーションを再生。ループ／非ループ再生と終了判定に対応。
- `Renderer`: DX12。実装は `RendererDevice/Models/Environment/Pipelines/PostProcess/Frame.cpp` に分割。GPU登録モデルは現在1種類、複数Instanceは可能。定数バッファはバックバッファ別領域。

## 実装済みゲーム機能
- `Scene_Game` が `Player` と `Enemy` 1体を登録。
- Player: WASD移動、Shift＋WASDで速度2倍のダッシュ、左クリックで攻撃。視点のY軸回転に合わせて本体と移動方向も回転。
- Playerモデル: `assets/model/SwordMen/sword_and_shield.glb`。`Idle` / `Walk` / `Run` / `Attack`を収録し、状態に応じて切り替える。Attackは1回再生後に移動状態へ復帰。
- MixamoアニメーションのHipsに含まれるローカルX/Y移動は固定し、ローカルZの上下動だけ適用する。
- Camera: Player追従、常時マウス視点操作、左右反転。Escでカーソル表示、左クリックで操作再開。Escは終了に使わない。
- Tree: `assets/texture/tree.png`を使った1枚ポリゴンのビルボードを地面に1本配置。
- Collider: Sphere / Y軸Capsule / AABB Box。全組合せの接触検出。
- PlayerとEnemyの接触時は互いに押し戻す。接触は`Collider::GetContacts()`から取得。Collider寸法はTransformの回転・拡縮と独立。

## アセット変換
- FBXは実行時に直接読まず、`.tools/convert_swordmen.py`でPlayer用GLBへまとめて変換する。
- Blender Portableは`.tools/BlenderPortable/`に配置済み。今後も使うため削除しない。
- Playerアニメーションを追加するときは、変換スクリプトの`clips`へ名前とFBXパスを追加して再実行する。

## 検証
```powershell
powershell -ExecutionPolicy Bypass -File tests/RunTests.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RenderEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```
- 最終確認: Debugビルド成功。ComponentManager/Collision/Engineテスト成功。
- ウィンドウ上の見た目・操作は最終変更後未確認。

## 作業状態
- リファクタリング、Collider追加、テスト追加を含む未コミット変更あり。勝手に破棄しない。
- `.claude/` は既存の未追跡物。変更しない。
- 次の機能は未決定。新チャットでは本ファイルを先に読み、依頼に関係するファイルだけ追加確認する。
