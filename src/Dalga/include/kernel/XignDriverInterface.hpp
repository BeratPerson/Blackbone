#pragma once
#include "XignDefs.hpp"
#include "interface/IDriverClient.hpp"
#include "kernel/WbmfAuth.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace Xign
{
    class Xhunter2Driver : public IDriverClient
    {
    public:
        Xhunter2Driver();
        explicit Xhunter2Driver(std::unique_ptr<IWbmfAuth> authManager);
        ~Xhunter2Driver() override;

        // IDriverClient implementation
        bool Open(const std::vector<uint8_t>& authBlob, const std::wstring& deviceName = DEFAULT_XHUNTER_DEVICE) override;
        void Close() override;
        bool IsReady() const override;

        bool SendCommand(uint32_t opcode,
                         const std::vector<uint8_t>& inPayload,
                         std::vector<uint8_t>& outResponse,
                         int32_t& outStatus,
                         uint64_t& outValue) override;

        bool RegisterProcess(uint32_t pid) override;
        bool OpenProcessHandle(uint32_t pid, uint64_t& outHandle) override;

        // Legacy / Convenience methods
        bool AuthenticateAndOpen(const std::vector<uint8_t>& wbmfDllBytes,
                                 const std::wstring& deviceName = DEFAULT_XHUNTER_DEVICE)
        {
            return Open(wbmfDllBytes, deviceName);
        }

        bool IsOpen() const { return IsReady(); }
        bool RegisterPidGate(uint32_t pid) { return RegisterProcess(pid); }
        bool OpenProtectedProcess(uint32_t pid, uint64_t& outHandle) { return OpenProcessHandle(pid, outHandle); }

        bool ReadRemoteMemory(uint64_t procHandle, uint64_t address, size_t size, std::vector<uint8_t>& out);
        bool StompKill(uint64_t handle);
        bool KernelInject(uint64_t pid, const std::vector<uint8_t>& shellcode);

    private:
        std::unique_ptr<IWbmfAuth> m_authManager;
#ifdef _WIN32
        void* m_handle = nullptr;
#endif
        bool m_open = false;
        uint64_t m_wbmfBase = 0;
        size_t m_wbmfSize = 0;
    };
}
