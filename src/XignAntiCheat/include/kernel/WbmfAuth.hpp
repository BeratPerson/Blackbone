#pragma once
#include <cstdint>
#include <vector>

namespace Xign
{
    class WbmfAuth
    {
    public:
        static bool MapWbmfModule(const std::vector<uint8_t>& dllBytes, uint64_t& peBase, size_t& peSize);
        static bool VerifyWbmfMagic(uint64_t peBase, size_t peSize);
    };
}
