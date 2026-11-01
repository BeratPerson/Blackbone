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
    Xhunter2Driver::Xhunter2Driver()
        : m_authManager(std::make_unique<WbmfAuthManager>())
    {
    }

    Xhunter2Driver::Xhunter2Driver(std::unique_ptr<IWbmfAuth> authManager)
        : m_authManager(std::move(authManager))
    {
        if (!m_authManager)
            m_authManager = std::make_unique<WbmfAuthManager>();
    }

    Xhunter2Driver::~Xhunter2Driver()
    {
        Close();
    }

    bool Xhunter2Driver::Open(const std::vector<uint8_t>& authBlob, const std::wstring& deviceName)
    {
        (void)deviceName;
        if (m_open) return true;

        if (authBlob.empty())
        {
            std::cerr << "[!] Authentication blob (WBMF DLL bytes) is empty.\n";
            return false;
        }

        if (!m_authManager->Initialize(authBlob))
        {
            std::cerr << "[!] WBMF authentication failed.\n";
            return false;
        }

        m_handle = m_authManager->GetDeviceHandle();
        m_wbmfBase = m_authManager->GetMappedBase();
        m_wbmfSize = m_authManager->GetMappedSize();

        if (!m_handle)
        {
            std::cerr << "[!] Valid device handle not acquired from authentication.\n";
            m_authManager->Shutdown();
            return false;
        }

        m_open = true;
        return true;
    }

    void Xhunter2Driver::Close()
    {
        m_open = false;
        if (m_authManager)
        {
            m_authManager->Shutdown();
        }
#ifdef _WIN32
        m_handle = nullptr;
        m_wbmfBase = 0;
        m_wbmfSize = 0;
#endif
    }

    bool Xhunter2Driver::IsReady() const
    {
        return m_open && (m_handle != nullptr);
    }

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

    bool Xhunter2Driver::RegisterProcess(uint32_t pid)
    {
        if (!IsReady() || pid == 0) return false;

        int32_t st = 0; uint64_t val = 0;
        std::vector<uint8_t> resp;
        std::vector<uint8_t> wbcc = AxHunterPacketBuilder::MakeWbcc(m_wbmfBase);

        uint8_t argBuf[XH2_REQ_SZ - XH2_ARG_OFF] = { 0 };
        *reinterpret_cast<uint32_t*>(argBuf) = pid;

        auto req = AxHunterPacketBuilder::BuildRequest(CMD_REGISTER, 0);
        std::memcpy(&req[XH2_ARG_OFF], argBuf, sizeof(argBuf));

        if (!SendCommand(CMD_REGISTER, wbcc, resp, st, val)) return false;
        if (st < 0) return false;
        if (!SendCommand(CMD_FLAG_30, wbcc, resp, st, val)) return false;
        if (!SendCommand(CMD_SET_FLAGS, wbcc, resp, st, val)) return false;
        return st == 0;
    }

    bool Xhunter2Driver::OpenProcessHandle(uint32_t pid, uint64_t& outHandle)
    {
        outHandle = 0;
        if (!IsReady() || pid == 0) return false;

        int32_t st = 0; uint64_t val = 0;
        std::vector<uint8_t> resp;
        std::vector<uint8_t> wbcc = AxHunterPacketBuilder::MakeWbcc(m_wbmfBase);

        uint8_t argBuf[XH2_REQ_SZ - XH2_ARG_OFF] = { 0 };
        *reinterpret_cast<uint32_t*>(argBuf) = pid;

        auto req = AxHunterPacketBuilder::BuildRequest(CMD_OPEN_PROC, 0);
        std::memcpy(&req[XH2_ARG_OFF], argBuf, sizeof(argBuf));

        if (!SendCommand(CMD_OPEN_PROC, wbcc, resp, st, val)) return false;
        outHandle = val;
        return st == 0;
    }

    // Full intended task: reads `size` bytes at `address` from the target process
    // identified by `procHandle`, through the driver's CMD_READ_MEM (787) command,
    // and copies the result into `out`. The stub below does not talk to the driver
    // yet, so it clears `out` and returns false.
    bool Xhunter2Driver::ReadRemoteMemory(uint64_t procHandle, uint64_t address, size_t size, std::vector<uint8_t>& out)
    {
        (void)procHandle; (void)address; (void)size;
        out.clear();
        return false;
    }

    // Full intended task: asks the driver to stomp the given handle in the target
    // process via CMD_HANDLE_STOMP (800), so the handle can no longer be used by the
    // target. The stub below does not send the command yet and returns false.
    bool Xhunter2Driver::StompKill(uint64_t handle)
    {
        (void)handle;
        return false;
    }

    // Full intended task: injects `shellcode` into the process `pid` from kernel mode.
    // It is meant to run CMD_INJECT_INIT (801) to prepare the target, then CMD_INJECT
    // (820) to write and execute the payload. The stub below does not send either
    // command yet and returns false.
    bool Xhunter2Driver::KernelInject(uint64_t pid, const std::vector<uint8_t>& shellcode)
    {
        (void)pid; (void)shellcode;
        return false;
    }
}
