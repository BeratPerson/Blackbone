#pragma once
#include "XignDefs.hpp"
#include <vector>
#include <string>
#include <functional>
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

    struct Xhunter2ObConfig
    {
        uint64_t PsProcessTypePtr;
        uint64_t PsThreadTypePtr;
        uint32_t Altitude;
        uint8_t  Padding[20];
    };

    struct ExportEntry
    {
        uint16_t Ordinal;
        uint32_t Rva;
        uint32_t Va;
        bool     IsActive;
    };

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

    struct AxHunterFrame
    {
        // Layout matching AxHunter proto.rs build_req()
        uint8_t  Raw[XH2_REQ_SZ];
    };
#pragma pack(pop)

    enum class X3Command : uint16_t
    {
        Heartbeat = 0x22,
        HwidScan = 0x23,
        WindowScan = 0x24,
        CodeIntegrity = 0x25
    };

    enum class XignDriverVersion : uint8_t
    {
        Xhunter1 = 1,
        Xhunter2 = 2,
        Unknown = 0
    };

    using X3DispatchHandler = std::function<bool(const uint8_t* payload, size_t len, std::vector<uint8_t>& response)>;
}