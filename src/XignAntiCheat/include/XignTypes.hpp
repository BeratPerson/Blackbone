#pragma once
#include "XignDefs.hpp"
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <cstdint>

namespace Xign
{
#pragma pack(push, 1)
    struct IoctlPacketHeader
    {
        uint32_t Magic;
        uint32_t Seed;
        uint16_t Command;
        uint16_t Flags;
        uint32_t PayloadLen;
    };

    struct AxHunterRequestLayout
    {
        // Bytes 0-23:   Unknown / padding (zeroed by driver)
        // Bytes 24-27:  REQ_SZ as u32  (0x4A0 = 1184)
        // Bytes 28-31:  MAGIC_XOR ^ SEED_KEY
        // Bytes 32-35:  SEED_KEY
        // Bytes 36-39:  Opcode (CMD_REGISTER etc.)
        // Bytes 40-47:  Response output pointer
        // Bytes 48-583: WBCC blob (536 bytes)
        // Bytes 584+:   Arguments (vary by opcode)
        uint8_t Raw[XH2_REQ_SZ];
    };
#pragma pack(pop)

    struct PeSectionInfo
    {
        std::string Name;
        uint32_t VirtualAddress;
        uint32_t VirtualSize;
        uint32_t RawAddress;
        uint32_t RawSize;
        uint32_t Characteristics;
        double Entropy;
    };

    struct ExportEntry
    {
        uint16_t Ordinal;
        uint32_t Rva;
        uint32_t Va;
        bool IsActive;
    };

    enum class XignDriverVersion : uint8_t
    {
        Xhunter1 = 1,
        Xhunter2 = 2,
        Unknown  = 0
    };
}