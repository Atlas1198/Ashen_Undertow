//-------------------------------------------------
//
//          GameAssets.h
//
//  実行時に読み込むモデルの相対パスを集約する。
//-------------------------------------------------

#pragma once

// 実行時に参照するモデルの相対パス。作業ディレクトリはプロジェクト直下。
namespace GameAssets
{
    inline constexpr char EnemyModel[] = "assets/model/Mutant/mutant_idle.glb";
    inline constexpr char PlayerModel[] =
        "assets/model/SwordMen/sword_and_shield.glb";
    inline constexpr char TreeModel[] = "assets/model/tree.gltf";
}
