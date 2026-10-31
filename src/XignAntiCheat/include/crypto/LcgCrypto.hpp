#pragma once
#include <vector>
#include <cstdint>

namespace Xign
{
    class LcgKeystream
    {
    public:
        explicit LcgKeystream(uint32_t seed);
        uint8_t NextByte();
        std::vector<uint8_t> Generate(size_t length);
        static std::vector<uint8_t> Crypt(const uint8_t* data, size_t len, uint32_t seed);
    private:
        uint32_t m_state;
        void Advance();
    };
}