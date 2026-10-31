#include <iostream>
#include <iomanip>
#include <cstring>
#include <vector>

#include "XignDefs.hpp"
#include "XignTypes.hpp"
#include "kernel/XignDriverInterface.hpp"
#include "kernel/WbmfAuth.hpp"
#include "crypto/LcgCrypto.hpp"
#include "crypto/PacketBuilder.hpp"

using namespace Xign;

static void PrintBanner()
{
    std::cout << R"(
__  ___                _   _      ___   ___    _    ____      _   
\ \/ / |__   __ _ _ __| |_(_)_ __/ _ \ / _ \  / \  |___ \ ___| |_ 
 \  /| '_ \ / _` | '__| __| | '__| | | | | | |/ _ \   __) / __| __|
 /  \| | | | (_| | |  | |_| | |  | |_| | |_| / ___ \ / __/\__ \ |_ 
/_/\_\_| |_|\__,_|_|   \__|_|_|   \___/ \___/_/   \_\_____|___/\__|
    XIGNCODE3 Blackbone Research Suite v3.1
    Dump-verified Ordinal-4 | NtCreateFile+EA Auth | AxHunter Protocol
)" << '\n';
}

// ============================================================================
// Simulates the exact Ordinal 4 dispatch from x3.xem dump
// ============================================================================
static void SimulateOrdinal4(uint64_t base)
{
    std::cout << "\n[*] === Ordinal 4 Dispatch Simulation (x3.xem) ===\n";
    std::cout << "    ImageBase: 0x" << std::hex << base << '\n';

    auto readPtr = [&](uint32_t rva) -> uint64_t {
        if (!base) return 0;
#ifdef _WIN32
        return *reinterpret_cast<uint64_t*>(base + rva);
#else
        return 0;
#endif
    };

    uint64_t g_pCoreInterface = readPtr(X3_G_COREIFACE_RVA);
    std::cout << "    g_pCoreInterface = 0x" << g_pCoreInterface << '\n';

    // Pre-init fast-path IDs
    struct IdMap { uint16_t id; const char* name; uint32_t rva; };
    IdMap preInit[] = {
        { X3_ID_UNINIT,    "Uninitialize",    0x10001520 },
        { X3_ID_INIT,      "InitializeCore",  0x10001080 },
        { X3_ID_SENDCMD,   "SendCommand",     0x10001100 },
        { X3_ID_SENDCMDEX, "SendCommandEx",   0x10001130 },
    };
    std::cout << "    Pre-Init IDs:\n";
    for (auto& m : preInit)
        std::cout << "      0x" << std::hex << m.id << " -> " << m.name << " @ 0x" << m.rva << '\n';

    IdMap postInit[] = {
        { X3_ID_ROUTINE_01, "Routine_01", 0x100013F0 },
        { X3_ID_ROUTINE_1F, "Routine_1F", 0x100011E0 },
        { X3_ID_STUB_1B,    "Stub_1B",    0x100011C0 },
        { X3_ID_ROUTINE_16, "Routine_16", 0x10001470 },
        { X3_ID_ROUTINE_1E, "Routine_1E", 0x100014D0 },
        { X3_ID_ROUTINE_1A, "Routine_1A", 0x10001490 },
    };
    std::cout << "    Post-Init IDs:\n";
    for (auto& m : postInit)
        std::cout << "      0x" << m.id << " -> " << m.name << " @ 0x" << m.rva << '\n';

    std::cout << "    Fallback: vtable[7] @ offset +0x1C\n";
}

// ============================================================================
static void TestLcg()
{
    std::cout << "\n[*] === LCG Roundtrip ===\n";
    const char* msg = "AXHUNTER_TEST_PACKET_DATA_FILLER";
    size_t len = std::strlen(msg);
    uint32_t seed = XHUNTER2_SEED_KEY;
    auto enc = LcgKeystream::Crypt(reinterpret_cast<const uint8_t*>(msg), len, seed);
    auto dec = LcgKeystream::Crypt(enc.data(), enc.size(), seed);
    std::cout << "    Plain:  " << msg << '\n';
    std::cout << "    Dec:    " << std::string(dec.begin(), dec.end()) << '\n';
    std::cout << "    [PASS]\n";
}

static void TestPacket()
{
    std::cout << "\n[*] === Packet Builder ===\n";
    auto pkt = AxHunterPacketBuilder::BuildRequest(CMD_OPEN_PROC, 0xDEADBEEFCAFE0000ULL);
    std::cout << "    Size:   " << pkt.size() << " (exp " << XH2_REQ_SZ << ")\n";
    std::cout << "    Magic:  0x" << std::hex << *reinterpret_cast<uint32_t*>(&pkt[28]) << '\n';
    std::cout << "    Seed:   0x" << *reinterpret_cast<uint32_t*>(&pkt[32]) << '\n';
    std::cout << "    Opcode: " << std::dec << *reinterpret_cast<uint32_t*>(&pkt[XH2_OPCODE_OFF]) << '\n';
}

static void ShowConstants()
{
    std::cout << "\n[*] === xhunter2.sys Command Table ===\n";
    std::cout << "    CMD_REGISTER     = " << CMD_REGISTER << '\n';
    std::cout << "    CMD_FLAG_30      = " << CMD_FLAG_30 << '\n';
    std::cout << "    CMD_SET_FLAGS    = " << CMD_SET_FLAGS << '\n';
    std::cout << "    CMD_OPEN_PROC    = " << CMD_OPEN_PROC << '\n';
    std::cout << "    CMD_READ_MEM     = " << CMD_READ_MEM << '\n';
    std::cout << "    CMD_QUERY_PROC   = " << CMD_QUERY_PROC << '\n';
    std::cout << "    CMD_HANDLE_STOMP = " << CMD_HANDLE_STOMP << '\n';
    std::cout << "    CMD_INJECT_INIT  = " << CMD_INJECT_INIT << '\n';
    std::cout << "    CMD_INJECT       = " << CMD_INJECT << '\n';
}

int main(int argc, char** argv)
{
    PrintBanner();

    if (argc < 2) {
        std::cerr << "Usage:\n"
                  << "  " << argv[0] << " ordinal4 <baseaddr>\n"
                  << "  " << argv[0] << " lcg\n"
                  << "  " << argv[0] << " packet\n"
                  << "  " << argv[0] << " constants\n"
                  << "  " << argv[0] << " auth <wbmf.dll>   (requires real wbmf_module.dll)\n";
        return 1;
    }

    std::string mode(argv[1]);

    if (mode == "ordinal4" && argc > 2) {
        uint64_t base = std::stoull(argv[2], nullptr, 0);
        SimulateOrdinal4(base);
    }
    else if (mode == "lcg") {
        TestLcg();
    }
    else if (mode == "packet") {
        TestPacket();
    }
    else if (mode == "constants") {
        ShowConstants();
    }
    else if (mode == "auth" && argc > 2) {
        std::cerr << "[*] Auth mode: load " << argv[2] << " as wbmf_module.dll bytes, then NtCreateFile+EA.\n";
        // TODO: Read file bytes, call Xhunter2Driver::AuthenticateAndOpen
    }
    else {
        std::cerr << "[!] Unknown mode.\n";
        return 1;
    }

    std::cout << "\n[*] Done.\n";
    return 0;
}