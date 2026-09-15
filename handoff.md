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
- `SceneManager`: `ObjectManager` を所有。更新順は `OnUpdate` → Object → Component → `CollisionManager` → `CollisionResolver` → `OnAfterUpdate`。その後描画。
- `ObjectManager::AddGameObject<T>()`: 生成・`Initialize`・所有・Update・Drawを自動管理。更新中に追加したObjectは次フレームから更新。
- `GameObject`: Transformと`ComponentManager`を所有。
- `ModelComponent`: モデル読込と個別World行列の描画。
- `ModelManager`: 正規化パス単位のCPUモデルキャッシュ。glTF/GLB対応、FBX未対応。
- `AnimationComponent`: glTFのスキニングアニメーションを再生。ループ／非ループ再生と終了判定に対応。
- `Rigidbody`: 重力、速度、接地状態を管理する物理Component。Colliderとの押し出し結果も速度へ反映する。
- `Collider`: `src/Engine/Collider.h` だけをincludeすれば、`SphereCollider` / `CapsuleCollider` / `BoxCollider` を利用できる。各ColliderはObject座標からのOffsetを個別指定可能。
- `CollisionResolver`: 接触後の押し出しを共通処理する。Rigidbodyを持つObjectを動的、持たないObjectを静的として扱う。Triggerは接触情報だけ更新し、押し出さない。
- `Renderer`: DX12。実装は `RendererDevice/Models/Environment/Pipelines/PostProcess/Frame/Debug.cpp` に分割。複数モデル・複数Instanceを描画可能。定数バッファはバックバッファ別領域。
- `GameHud`: Boss・MiniCopy・PlayerのHP／スタミナ表示を担当。`Scene_Game` はゲーム状態を渡すだけ。
- `GameBalance.h`: Player・Boss・MiniCopy・Sceneの調整値を集約。速度、攻撃判定時間、スタミナ消費量などはここから変更する。
- `Player`: フレーム更新を接地、ジャンプ／回避、チャージ、通常攻撃、移動、スタミナへ分割済み。
- `EffectManager`: 公開APIは1つのまま、実装をCore・Combat・Charge・SwordTrail・Environmentへ分割済み。
- `RendererInternal`: D3D12のヒープ／バッファ記述子生成を共通ヘルパー化し、Renderer実装間の重複を削減。

## 実装済みゲーム機能
- `Scene_Game` が `Player` と `Enemy` 1体を登録。
- Player: WASD移動、Shift＋WASDで速度2倍のダッシュ、左クリックで攻撃、Spaceでジャンプ。Rigidbodyで重力と上下速度を処理する。視点のY軸回転に合わせて本体と移動方向も回転。
- 必殺技: ミニコピー撃破ごとに虹色ゲージが10%増加。満タン時はPlayerのリムが虹色に点灯し、FでBossへ自動追尾する三日月型斬撃波を発射する。着弾時はBossへ直接1000ダメージ。調整値は`GameBalance::SpecialAttack`。
- Playerモデル: `assets/model/SwordMen/sword_and_shield.glb`。`Idle` / `Walk` / `Run` / `Attack` / `Jump`を収録し、状態に応じて切り替える。Attackは1回再生後に移動状態へ復帰する。
- Player Collider: `CapsuleCollider(0.4f, 0.4f)`、Offsetは`(0, -0.3, 0)`。
- Enemyモデル: `assets/model/Mutant/mutant_idle.glb`。`Mutant Breathing Idle.fbx`から変換したIdleアニメーションをループ再生する。Colliderは`CapsuleCollider(0.4f, 0.4f)`、Offsetは`(0, 0.8, 0)`。
- MixamoアニメーションのHipsに含まれるローカルX/Y移動は固定し、ローカルZの上下動だけ適用する。
- Camera: Player追従、常時マウス視点操作、左右反転。Escでカーソル表示、左クリックで操作再開。Escは終了に使わない。
- Tree: `assets/texture/tree.png`を使った1枚ポリゴンのビルボードを地面に1本配置。
- Collider: Sphere / Y軸Capsule / AABB Box。全組合せの接触検出。寸法とOffsetはTransformの回転・拡縮と独立。
- Colliderは`SetTrigger`でTrigger化でき、`IsTriggering` / `HasContactWith` / `GetContacts`で接触を参照できる。
- Object同士の押し出しとTrigger処理は`CollisionResolver`へ集約済み。`Scene_Game`には個別の押し出し処理を書かない。
- CキーでCollider表示を切り替える。通常は緑、接触中は赤で描画する。

## アセット変換
- FBXは実行時に直接読まず、`.tools/convert_swordmen.py`でPlayer用GLBへまとめて変換する。
- Enemyは`.tools/convert_mutant.py`で`Mutant Breathing Idle.fbx`を`assets/model/Mutant/mutant_idle.glb`へ変換する。
- Blender Portableは`.tools/BlenderPortable/`に配置済み。今後も使うため削除しない。
- Playerアニメーションを追加するときは、変換スクリプトの`clips`へ名前とFBXパスを追加して再実行する。

## 検証
```powershell
powershell -ExecutionPolicy Bypass -File tests/RunTests.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RenderEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```
- 最終確認: Debugビルド成功。全8テスト成功。
- ウィンドウ上の見た目・操作は最終変更後未確認。

## 作業状態
- `.claude/` は既存の未追跡物。変更しない。今回の更新前は、それ以外に`git status --short`上の変更なし。
- 次の機能は未決定。新チャットでは本ファイルを先に読み、依頼に関係するファイルだけ追加確認する。
