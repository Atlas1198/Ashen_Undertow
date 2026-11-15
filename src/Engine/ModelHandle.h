//-------------------------------------------------
//
//          ModelHandle.h
//
//  読み込み済みモデルを参照するハンドルを定義する。
//-------------------------------------------------

#pragma once

#include <cstdint>

// ModelManager 内の添字。無効値を持つと読み込み失敗／未設定を表す。
struct ModelHandle
{
    static constexpr uint32_t InvalidValue = UINT32_MAX;
    uint32_t value = InvalidValue;
    bool IsValid() const { return value != InvalidValue; }
    bool operator==(const ModelHandle&) const = default;
};
