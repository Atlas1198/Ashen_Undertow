#pragma once

#include <cstdint>

struct ModelHandle
{
    static constexpr uint32_t InvalidValue = UINT32_MAX;
    uint32_t value = InvalidValue;
    bool IsValid() const { return value != InvalidValue; }
    bool operator==(const ModelHandle&) const = default;
};
