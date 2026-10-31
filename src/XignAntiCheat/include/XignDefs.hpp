#pragma once
#include <cstdint>
#include <cstddef>

namespace Xign
{
    constexpr uint32_t LCG_INIT_ADDEND    = 1864026567u;
    constexpr uint32_t LCG_INIT_MUL       = 1640531535u;
    constexpr uint32_t LCG_XOR_KEY        = 0x7B85F243u;
    constexpr uint32_t LCG_STEP_MUL       = 214013u;
    constexpr uint32_t LCG_STEP_ADD       = 2531011u;
    constexpr uint32_t LCG_DECRYPT_XOR    = 0x7B85F243u;

    constexpr size_t   XH2_REQ_SZ         = 512;
    constexpr size_t   XH2_RESP_SZ        = 512;
    constexpr size_t   XH2_WBCC_LEN       = 32;
    constexpr size_t   XH2_WBCC_OFF       = 48;
    constexpr size_t   XH2_OPCODE_OFF     = 36;
    constexpr size_t   XH2_RESP_PTR_OFF   = 40;
    constexpr size_t   XH2_ARG_OFF        = 80;

    constexpr uint32_t XHUNTER2_MAGIC_XOR = 0x58485432;
    constexpr uint32_t XHUNTER2_SEED_KEY  = 0x13371337;

    constexpr uint32_t CMD_REGISTER       = 777;
    constexpr uint32_t CMD_OPEN_PROC      = 780;
}
