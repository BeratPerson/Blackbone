#include "crypto/PacketBuilder.hpp"
#include "XignDefs.hpp"
#include <cstring>

namespace Xign
{
    std::vector<uint8_t> AxHunterPacketBuilder::BuildRequest(uint32_t opcode, uint64_t respPtr, uint32_t seed)
    {
        std::vector<uint8_t> req(XH2_REQ_SZ, 0);
        // offset 24..28: REQ_SZ as u32
        *reinterpret_cast<uint32_t*>(&req[24]) = static_cast<uint32_t>(XH2_REQ_SZ);
        // offset 28..32: MAGIC_XOR ^ SEED_KEY
        *reinterpret_cast<uint32_t*>(&req[28]) = XHUNTER2_MAGIC_XOR ^ seed;
        // offset 32..36: SEED_KEY
        *reinterpret_cast<uint32_t*>(&req[32]) = seed;
        // offset 36..40: opcode
        *reinterpret_cast<uint32_t*>(&req[XH2_OPCODE_OFF]) = opcode;
        // offset 40..48: response pointer
        std::memcpy(&req[XH2_RESP_PTR_OFF], &respPtr, sizeof(respPtr));
        return req;
    }

    bool AxHunterPacketBuilder::EncryptRequest(std::vector<uint8_t>& buf)
    {
        if (buf.size() < XH2_REQ_SZ) return false;
        uint32_t sk = *reinterpret_cast<uint32_t*>(&buf[32]);
        LcgEncryptBuffer(buf.data() + 36, buf.size() - 36, sk);
        return true;
    }

    bool AxHunterPacketBuilder::DecryptResponse(std::vector<uint8_t>& buf)
    {
        if (buf.size() < XH2_RESP_SZ) return false;
        LcgDecryptBuffer(buf.data() + 36, buf.size() - 36);
        return true;
    }

    std::vector<uint8_t> AxHunterPacketBuilder::MakeWbcc(uint64_t peBase)
    {
        std::vector<uint8_t> b(XH2_WBCC_LEN, 0);
        std::memcpy(&b[0], "WBCC", 4);
        *reinterpret_cast<uint16_t*>(&b[4]) = static_cast<uint16_t>(XH2_WBCC_LEN);
        *reinterpret_cast<uint16_t*>(&b[6]) = 1;
        std::memcpy(&b[16], &peBase, sizeof(peBase));
        return b;
    }

    void AxHunterPacketBuilder::LcgEncryptBuffer(uint8_t* data, size_t len, uint32_t sk)
    {
        uint32_t s = LCG_INIT_ADDEND - (LCG_INIT_MUL * (sk ^ LCG_XOR_KEY));
        for (size_t i = 0; i < len; ++i)
        {
            s = (s * LCG_STEP_MUL) + LCG_STEP_ADD;
            data[i] ^= static_cast<uint8_t>((s >> 23) & 0xFF);
        }
    }

    void AxHunterPacketBuilder::LcgDecryptBuffer(uint8_t* data, size_t len)
    {
        uint32_t s = LCG_INIT_ADDEND - (LCG_INIT_MUL * (XHUNTER2_SEED_KEY ^ LCG_DECRYPT_XOR));
        for (size_t i = 0; i < len; ++i)
        {
            s = (s * LCG_STEP_MUL) + LCG_STEP_ADD;
            data[i] ^= static_cast<uint8_t>((s >> 23) & 0xFF);
        }
    }
}