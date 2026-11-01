#pragma once
#include <cstdint>
#include <vector>

namespace Xign
{
    enum class TransportStatus
    {
        Success,
        DeviceNotReady,
        InvalidBufferSize,
        Timeout,
        CommunicationError
    };

    struct TransportResult
    {
        TransportStatus Status{ TransportStatus::DeviceNotReady };
        int32_t DriverStatus{ -1 };
        uint64_t DriverValue{ 0 };
        std::vector<uint8_t> ResponseData{};
    };

    class IIoctlTransport
    {
    public:
        virtual ~IIoctlTransport() = default;

        virtual bool Connect(void* deviceHandle) = 0;
        virtual void Disconnect() = 0;
        virtual bool IsConnected() const = 0;

        virtual TransportResult Transceive(uint32_t opcode,
                                           const std::vector<uint8_t>& requestBuffer,
                                           size_t expectedResponseSize) = 0;
    };
}
