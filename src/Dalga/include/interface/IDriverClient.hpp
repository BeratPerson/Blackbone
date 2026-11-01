#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace Xign
{
    class IDriverClient
    {
    public:
        virtual ~IDriverClient() = default;

        virtual bool Open(const std::vector<uint8_t>& authBlob, const std::wstring& deviceName) = 0;
        virtual void Close() = 0;
        virtual bool IsReady() const = 0;

        virtual bool SendCommand(uint32_t opcode,
                                 const std::vector<uint8_t>& inPayload,
                                 std::vector<uint8_t>& outResponse,
                                 int32_t& outStatus,
                                 uint64_t& outValue) = 0;

        virtual bool RegisterProcess(uint32_t pid) = 0;
        virtual bool OpenProcessHandle(uint32_t pid, uint64_t& outHandle) = 0;
    };
}
