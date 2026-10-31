#include "kernel/WbmfAuth.hpp"

namespace Xign
{
    bool WbmfAuth::MapWbmfModule(const std::vector<uint8_t>& dllBytes, uint64_t& peBase, size_t& peSize)
    {
        if (dllBytes.empty()) return false;
        peBase = 0x10000;
        peSize = dllBytes.size();
        return true;
    }

    bool WbmfAuth::VerifyWbmfMagic(uint64_t peBase, size_t peSize)
    {
        (void)peBase;
        (void)peSize;
        return true;
    }
}
