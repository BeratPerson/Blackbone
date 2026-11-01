#include "LcgCrypto.hpp"
#include "XignDefs.hpp"

namespace Xign
{
    LcgKeystream::LcgKeystream(uint32_t seed)
    {
        m_state = LCG_INIT_ADDEND - (LCG_INIT_MUL * (seed ^ LCG_XOR_KEY));
    }

    void LcgKeystream::Advance()
    {
        m_state = (m_state * LCG_STEP_MUL) + LCG_STEP_ADD;
    }

    uint8_t LcgKeystream::NextByte()
    {
        Advance();
        return static_cast<uint8_t>((m_state >> 23) & 0xFF);
    }

    std::vector<uint8_t> LcgKeystream::Generate(size_t length)
    {
        std::vector<uint8_t> out;
        out.reserve(length);
        for (size_t i = 0; i < length; ++i)
            out.push_back(NextByte());
        return out;
    }

    std::vector<uint8_t> LcgKeystream::Crypt(const uint8_t* data, size_t len, uint32_t seed)
    {
        if (!data || len == 0) return {};
        LcgKeystream ks(seed);
        std::vector<uint8_t> out;
        out.reserve(len);
        for (size_t i = 0; i < len; ++i)
            out.push_back(data[i] ^ ks.NextByte());
        return out;
    }
}