#include "XignDriverInterface.hpp"
#include "WbmfAuth.hpp"
#include "crypto/PacketBuilder.hpp"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
extern "C" __declspec(dllimport) uint32_t __stdcall NtClose(void* Handle);
#endif

namespace Xign
{
    Xhunter2Driver::Xhunter2Driver() = default;
    Xhunter2Driver::~Xhunter2Driver() { Close(); }

    bool Xhunter2Driver::AuthenticateAndOpen(const std::vector<uint8_t>& wbmfDllBytes,
                                             const std::wstring& deviceName)
    {
#ifdef _WIN32
        if (m_open) return true;

        if (!WbmfAuth::MapWbmfModule(wbmfDllBytes, m_wbmfBase, m_wbmfSize)) {
            std::cerr << "[!] WBMF map failed.\n";
            return false;
        }
        if (!WbmfAuth::VerifyWbmfMagic(m_wbmfBase, m_wbmfSize)) {
            std::cerr << "[!] WBMF magic fail.\n";
            return false;
        }

        // Determine arch
        uint32_t lfanew = *reinterpret_cast<uint32_t*>(m_wbmfBase + 0x3c);
        uint16_t machine = *reinterpret_cast<uint16_t*>(m_wbmfBase + lfanew + 4);
        bool isX64 = (machine == IMAGE_FILE_MACHINE_AMD64);

        // Trampoline target: we jump to a small stub inside our mapped region.
        // In practice this should point to the auth routine inside wbmf.
        uint64_t trampTarget = m_wbmfBase + WBMF_TRAMPOLINE_OFFSET + 0x1000;
        if (!WbmfAuth::InstallTrampoline(m_wbmfBase, WBMF_TRAMPOLINE_OFFSET, trampTarget, isX64)) {
            std::cerr << "[!] Trampoline install fail.\n";
            return false;
        }

        auto eaBuf = WbmfAuth::BuildEaBuffer(m_wbmfBase);
        void* hDevice = nullptr;
        if (!WbmfAuth::OpenDeviceHandle(deviceName, eaBuf, &hDevice)) {
            std::cerr << "[!] NtCreateFile fail.\n";
            return false;
        }
        m_handle = hDevice;

        if (!WbmfAuth::ExecuteWbmfTrampoline(m_wbmfBase, WBMF_TRAMPOLINE_OFFSET)) {
            std::cerr << "[!] Trampoline thread fail.\n";
            return false;
        }

        m_open = true;
        return true;
#else
        return false;
#endif
    }

    void Xhunter2Driver::Close()
    {
        m_open = false;
#ifdef _WIN32
        if (m_handle) {
            NtClose(m_handle);
            m_handle = nullptr;
        }
        if (m_wbmfBase) {
            VirtualFree(reinterpret_cast<void*>(m_wbmfBase), 0, MEM_RELEASE);
            m_wbmfBase = 0;
        }
#endif
    }

    bool Xhunter2Driver::IsOpen() const { return m_open; }

    bool Xhunter2Driver::SendCommand(uint32_t opcode, const std::vector<uint8_t>& wbccBlob,
                                     std::vector<uint8_t>& outResponse, int32_t& outStatus, uint64_t& outValue)
    {
        if (!m_open || !m_handle) return false;
        outResponse.resize(XH2_RESP_SZ, 0);

        auto req = AxHunterPacketBuilder::BuildRequest(opcode, reinterpret_cast<uint64_t>(outResponse.data()));
        if (wbccBlob.size() == XH2_WBCC_LEN)
            std::memcpy(&req[XH2_WBCC_OFF], wbccBlob.data(), XH2_WBCC_LEN);

        if (!AxHunterPacketBuilder::EncryptRequest(req)) return false;

#ifdef _WIN32
        DWORD written = 0;
        BOOL ok = WriteFile(static_cast<HANDLE>(m_handle), req.data(), static_cast<DWORD>(req.size()), &written, nullptr);
        if (!ok) return false;
#endif

        AxHunterPacketBuilder::DecryptResponse(outResponse);
        outStatus = *reinterpret_cast<int32_t*>(&outResponse[36]);
        outValue  = *reinterpret_cast<uint64_t*>(&outResponse[40]);
        return true;
    }

    bool Xhunter2Driver::RegisterPidGate(uint32_t pid)
    {
        int32_t st = 0; uint64_t val = 0;
        std::vector<uint8_t> resp;
        std::vector<uint8_t> wbcc = AxHunterPacketBuilder::MakeWbcc(m_wbmfBase);

        uint8_t argBuf[XH2_REQ_SZ - XH2_ARG_OFF] = {0};
        *reinterpret_cast<uint32_t*>(argBuf) = pid;

        auto req = AxHunterPacketBuilder::BuildRequest(CMD_REGISTER, 0);
        std::memcpy(&req[XH2_ARG_OFF], argBuf, sizeof(argBuf));

        if (!SendCommand(CMD_REGISTER, wbcc, resp, st, val)) return false;
        if (st < 0) return false;
        if (!SendCommand(CMD_FLAG_30, wbcc, resp, st, val)) return false;
        if (!SendCommand(CMD_SET_FLAGS, wbcc, resp, st, val)) return false;
        return st == 0;
    }

    bool Xhunter2Driver::OpenProtectedProcess(uint32_t pid, uint64_t& outHandle)
    {
        int32_t st = 0; uint64_t val = 0;
        std::vector<uint8_t> resp;
        std::vector<uint8_t> wbcc = AxHunterPacketBuilder::MakeWbcc(m_wbmfBase);

        uint8_t argBuf[XH2_REQ_SZ - XH2_ARG_OFF] = {0};
        *reinterpret_cast<uint32_t*>(argBuf) = pid;

        auto req = AxHunterPacketBuilder::BuildRequest(CMD_OPEN_PROC, 0);
        std::memcpy(&req[XH2_ARG_OFF], argBuf, sizeof(argBuf));

        if (!SendCommand(CMD_OPEN_PROC, wbcc, resp, st, val)) return false;
        outHandle = val;
        return st == 0;
    }

    bool Xhunter2Driver::ReadRemoteMemory(uint64_t procHandle, uint64_t address, size_t size, std::vector<uint8_t>& out)
    {
        (void)procHandle; (void)address; (void)size;
        out.clear();
        // TODO: Arg layout for CMD_READ_MEM (787) needs live trace verification.
        // Expected: argBuf[0..7]=handle, argBuf[8..15]=address, argBuf[16..23]=size
        return false;
    }

    bool Xhunter2Driver::StompKill(uint64_t handle)
    {
        (void)handle;
        // TODO: CMD_HANDLE_STOMP (800) arg layout: handle in argBuf[0..7]
        return false;
    }

    bool Xhunter2Driver::KernelInject(uint64_t pid, const std::vector<uint8_t>& shellcode)
    {
        (void)pid; (void)shellcode;
        // TODO: CMD_INJECT_INIT (801) then CMD_INJECT (820) payload format unknown
        return false;
    }
}