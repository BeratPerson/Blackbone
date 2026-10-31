#pragma once
#include <cstdint>

namespace Xign
{
    // ── xhunter1.sys (legacy v2023.12.7.78) ──
    constexpr uint32_t XHUNTER1_MAGIC           = 0x345821ab;
    constexpr uint64_t XHUNTER1_DRIVERENTRY_VA    = 0x140024000;
    constexpr uint64_t XHUNTER1_IRP_MJ_CREATE   = 0x14000b4f4;
    constexpr uint64_t XHUNTER1_IRP_MJ_CLOSE    = 0x14000b138;
    constexpr uint64_t XHUNTER1_IRP_MJ_READ     = 0x14000b0d0;
    constexpr uint64_t XHUNTER1_IRP_MJ_DEVICE_CONTROL = 0x14000b178;
    constexpr uint32_t XHUNTER1_OB_ALTITUDE     = 380800;

    // ── xhunter2.sys (current v2026.9.16.465 / v2026.6.1.192) ──
    constexpr uint32_t XHUNTER2_MAGIC_XOR       = 0x70506202; // MAGIC ^ SEED_KEY
    constexpr uint32_t XHUNTER2_SEED_KEY        = 0x41414141;
    constexpr uint64_t XHUNTER2_DRIVERENTRY_VA  = 0x140033000;
    constexpr uint64_t XHUNTER2_IRP_MJ_CLOSE    = 0x140012bc4;
    constexpr uint64_t XHUNTER2_IRP_MJ_CLEANUP  = 0x140012ab4;
    constexpr uint64_t XHUNTER2_IRP_MJ_READ     = 0x140012aec;
    constexpr uint64_t XHUNTER2_IRP_MJ_DEVICE_CONTROL = 0x140012cc0;
    constexpr uint64_t XHUNTER2_VERIFY_CALLER  = 0x1400040ac;
    constexpr uint64_t XHUNTER2_SCAN_UNBACKED  = 0x140014010;
    constexpr uint64_t XHUNTER2_REGISTER_CB    = 0x14000a978;
    constexpr uint64_t XHUNTER2_PROC_PRE_CB    = 0x14000acc8;
    constexpr uint64_t XHUNTER2_PROC_POST_CB   = 0x14000ae00;
    constexpr uint64_t XHUNTER2_THREAD_PRE_CB  = 0x14000af0c;
    constexpr uint64_t XHUNTER2_LOADIMAGE_PATCHER = 0x14000a5a0;
    constexpr uint64_t XHUNTER2_SHADOW_SSDT    = 0x140043070;
    constexpr uint64_t XHUNTER2_MINIFILTER     = 0x14003fa3a;
    constexpr uint64_t XHUNTER2_ANTITAMPER     = 0x14003f371;
    constexpr uint32_t XHUNTER2_OB_ALTITUDE    = 363390;

    // AxHunter IRP_MJ_WRITE protocol
    constexpr size_t   XH2_REQ_SZ         = 1184;
    constexpr size_t   XH2_RESP_SZ        = 786;
    constexpr size_t   XH2_OPCODE_OFF   = 36;
    constexpr size_t   XH2_RESP_PTR_OFF = 40;
    constexpr size_t   XH2_WBCC_OFF     = 48;
    constexpr size_t   XH2_WBCC_LEN     = 536;
    constexpr size_t   XH2_ARG_OFF      = 584;

    // AxHunter direct opcodes
    constexpr uint32_t CMD_REGISTER     = 777;
    constexpr uint32_t CMD_FLAG_30      = 779;
    constexpr uint32_t CMD_SET_FLAGS    = 775;
    constexpr uint32_t CMD_OPEN_PROC    = 785;
    constexpr uint32_t CMD_READ_MEM     = 787;
    constexpr uint32_t CMD_QUERY_PROC   = 791;
    constexpr uint32_t CMD_HANDLE_STOMP = 800;
    constexpr uint32_t CMD_INJECT_INIT  = 801;
    constexpr uint32_t CMD_INJECT       = 820;

    constexpr uint32_t PROC_ALL_ACCESS  = 0x1FFFFF;

    // ── x3.xem (x3_barrier_Win32.dll) PE32 ──
    constexpr uint32_t X3_IMAGE_BASE      = 0x10000000;
    constexpr uint32_t X3_ORDINAL1_RVA    = 0x1540;
    constexpr uint32_t X3_ORDINAL2_RVA    = 0x1060;
    constexpr uint32_t X3_ORDINAL3_RVA    = 0x1070;
    constexpr uint32_t X3_ORDINAL4_RVA    = 0x1600;   // Xigncode_GetProcAddressEx
    constexpr uint32_t X3_ORDINAL99_RVA   = 0x2A9058;

    // Ordinal 4 Pre-Init static IDs (VA = IMAGE_BASE + RVA)
    constexpr uint16_t X3_ID_INIT         = 0x22;   // -> 0x10001080  (InitializeCore)
    constexpr uint16_t X3_ID_UNINIT       = 0x23;   // -> 0x10001520  (Uninitialize)
    constexpr uint16_t X3_ID_SENDCMD      = 0x24;   // -> 0x10001100  (SendCommand)
    constexpr uint16_t X3_ID_SENDCMDEX    = 0x25;   // -> 0x10001130  (SendCommandEx)

    // Ordinal 4 Post-Init static IDs
    constexpr uint16_t X3_ID_ROUTINE_01   = 0x01;   // -> 0x100013F0
    constexpr uint16_t X3_ID_ROUTINE_1F   = 0x1F;   // -> 0x100011E0
    constexpr uint16_t X3_ID_STUB_1B      = 0x1B;   // -> 0x100011C0  (xor eax, eax; ret 4)
    constexpr uint16_t X3_ID_ROUTINE_16   = 0x16;   // -> 0x10001470
    constexpr uint16_t X3_ID_ROUTINE_1E   = 0x1E;   // -> 0x100014D0
    constexpr uint16_t X3_ID_ROUTINE_1A   = 0x1A;   // -> 0x10001490

    // .data globals (RVA from ImageBase)
    constexpr uint32_t X3_G_COREIFACE_RVA = 0x002A904C; // g_pCoreInterface
    constexpr uint32_t X3_G_CONFIG_RVA    = 0x002A9050; // g_ConfigData

    // Status codes
    constexpr uint32_t X3_STATUS_SUCCESS        = 0x00000000;
    constexpr uint32_t X3_ERROR_NOT_INITIALIZED = 0xE0010001;

    // ── LCG (AxHunter verified) ──
    constexpr uint32_t LCG_INIT_ADDEND = 1864026567; // 0x6F1AC9C7
    constexpr uint32_t LCG_INIT_MUL    = 1640531535; // 0x61C8864F
    constexpr uint32_t LCG_XOR_KEY     = 0x7B85F243;
    constexpr uint32_t LCG_STEP_MUL    = 1664525;    // 0x0019660D
    constexpr uint32_t LCG_STEP_ADD    = 1013904223;   // 0x3C6EF35F
    constexpr uint32_t LCG_DECRYPT_XOR = 0x847A0DBC;

    // WBMF
    constexpr uint32_t WBMF_MAGIC_OFFSET      = 0x290A0;
    constexpr uint32_t WBMF_TRAMPOLINE_OFFSET = 0x500;
    constexpr uint32_t WBCT_VAL               = 536;
    constexpr uint32_t EA_HDR_LEN             = 13;
    constexpr uint32_t EA_TOTAL_LEN           = EA_HDR_LEN + WBCT_VAL;

    // Device
    constexpr wchar_t const* DEFAULT_XHUNTER_DEVICE = L"\\Device\\xhunter2";
}