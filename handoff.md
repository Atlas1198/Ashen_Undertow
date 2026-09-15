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
- `GameHud`: Boss・MiniCopy・PlayerのHP／スタミナ／必殺技ゲージ表示を担当。PlayerのHP減少も検出し、被弾時の赤色化・揺れ・脈動を約0.38秒再生する。
- `GameBalance.h`: Player・Boss・MiniCopy・Sceneの調整値を集約。速度、攻撃判定時間、スタミナ消費量などはここから変更する。
- `Player`: フレーム更新を接地、ジャンプ／回避、チャージ、通常攻撃、移動、スタミナへ分割済み。
- `EffectManager`: 公開APIは1つのまま、実装をCore・Combat・Charge・SwordTrail・Environment・Specialへ分割済み。
- `RendererInternal`: D3D12のヒープ／バッファ記述子生成を共通ヘルパー化し、Renderer実装間の重複を削減。

## 実装済みゲーム機能
- `Scene_Game` が`Player`・ステージ外の`Boss`・Bossが召喚する`MiniCopy`群を管理する。
- Player: WASD移動、Shift＋WASDで速度2倍のダッシュ、左クリックで攻撃、Spaceでジャンプ。Rigidbodyで重力と上下速度を処理する。視点のY軸回転に合わせて本体と移動方向も回転。
- 必殺技: ミニコピー撃破ごとにパステル虹色ゲージが10%増加。満タン時はPlayerのリムが虹色に点灯し、FでBossへ自動追尾する大型の三日月型斬撃波を発射する。現在は動作確認用に開始時から満タン。
- 斬撃波は進行方向から見ると一本線、側面から三日月に見える向きで描画する。Bossの顔ボーンを追尾して直接1000ダメージを与える。調整値は`GameBalance::SpecialAttack`。
- 必殺技の着弾判定は顔ボーン中心のまま、着弾エフェクトだけを斬撃波の進行方向と逆側へ`Boss::GetVisualScale() * 0.14f`移動し、モデル内部への埋まりを防止する。
- 必殺技着弾エフェクトは三層の巨大フラッシュ、放射火花320本、上昇して散る残光80個で構成する。実装は`src/Engine/EffectManagerSpecial.cpp`。
- PlayerのHPバーは被弾時に約0.38秒だけ赤くなり、減衰する横揺れと軽い縦脈動を行う。連続被弾時は演出時間をリセットする。
- Playerモデル: `assets/model/SwordMen/sword_and_shield.glb`。`Idle` / `Walk` / `Run` / `Attack` / `Jump`を収録し、状態に応じて切り替える。Attackは1回再生後に移動状態へ復帰する。
- Player Collider: 胴体・両腕・両脚・頭のボーン追従Hurtbox、剣の通常／チャージ攻撃用Segment Hitbox、回避中のSphere Hurtboxで構成する。胴体Hurtboxは非Triggerで押し出しも担当する。
- Boss／MiniCopyモデルはMutant系GLBを使用。MiniCopyはボーン追従の全身Hurtboxと左右武器Hitboxを持つ。
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

## 直近の実装
- `Scene_Title`から本編／チュートリアルを選択可能。上下キー・W/S・Enterに対応。
- `Scene_Tutorial`は8操作を順番に判定し、Player HUDも表示する。溜め攻撃中はスタミナバーを水色で強調し、説明に「スタミナ消費」を表示。
- チュートリアル完了時は`CLEAR`を表示。Enter後に暗転してGameSceneへ移動する。
- GameScene開始時はステージ上へ`game_start.png`を約2.2秒フェードイン／アウト表示し、その間はゲーム進行とHUDを停止する。表示位置はY=240。
- `AudioManager`でBGM／SEを管理。選択・決定音は使用せず、PlayerとEnemyへ草地の足音を追加済み（Enemyは小音量）。
- Player死亡後は`YOU DIED`＋TITLE/RETRY、Boss撃破後は`CLEAR`＋TITLEを表示する。

## 検証
```powershell
powershell -ExecutionPolicy Bypass -File tests/RunTests.ps1
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe" RenderEngine.vcxproj /p:Configuration=Debug /p:Platform=x64 /m /v:minimal
```
- 最終確認: Debugビルド成功。テストスクリプト終了コード0。
- ウィンドウ上の必殺技着弾位置・巨大エフェクト・Player HPバー被弾演出は最終変更後未確認。

## 作業状態
- `.claude/` は既存の未追跡物。変更しない。作業ツリーには必殺技関連を含む進行中の変更があるため、まとめて破棄しない。
- 次の機能は未決定。新チャットでは本ファイルを先に読み、依頼に関係するファイルだけ追加確認する。
