#pragma once
#include "XignDefs.hpp"
#include <vector>
#include <string>
#include <cstdint>

namespace Xign
{
    class Xhunter2Driver
    {
    public:
        Xhunter2Driver();
        ~Xhunter2Driver();

        bool AuthenticateAndOpen(const std::vector<uint8_t>& wbmfDllBytes,
                                 const std::wstring& deviceName = DEFAULT_XHUNTER_DEVICE);

        void Close();
        bool IsOpen() const;

        bool SendCommand(uint32_t opcode,
                         const std::vector<uint8_t>& wbccBlob,
                         std::vector<uint8_t>& outResponse,
                         int32_t& outStatus,
                         uint64_t& outValue);

        bool RegisterPidGate(uint32_t pid);
        bool OpenProtectedProcess(uint32_t pid, uint64_t& outHandle);
        bool ReadRemoteMemory(uint64_t procHandle, uint64_t address, size_t size, std::vector<uint8_t>& out);
        bool StompKill(uint64_t handle);
        bool KernelInject(uint64_t pid, const std::vector<uint8_t>& shellcode);

    private:
#ifdef _WIN32
        void* m_handle = nullptr;
#endif
        bool m_open = false;
        uint64_t m_wbmfBase = 0;
        size_t m_wbmfSize = 0;
    };
}