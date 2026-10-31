#pragma once
#include <cstdint>
#include <vector>

namespace Xign
{
    class AxHunterPacketBuilder
    {
    public:
        static std::vector<uint8_t> BuildRequest(uint32_t opcode, uint64_t respPtr, uint32_t seed = 0x1337);
        static bool EncryptRequest(std::vector<uint8_t>& buf);
        static bool DecryptResponse(std::vector<uint8_t>& buf);
        static std::vector<uint8_t> MakeWbcc(uint64_t peBase);
        static void LcgEncryptBuffer(uint8_t* data, size_t len, uint32_t sk);
        static void LcgDecryptBuffer(uint8_t* data, size_t len);
    };
}
