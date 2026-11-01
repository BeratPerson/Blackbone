#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace Xign
{
    enum class AuthState
    {
        Uninitialized,
        Mapped,
        Authenticated,
        Failed
    };

    class IWbmfAuth
    {
    public:
        virtual ~IWbmfAuth() = default;

        virtual bool Initialize(const std::vector<uint8_t>& wbmfDllBytes) = 0;
        virtual void Shutdown() = 0;
        virtual bool IsAuthenticated() const = 0;
        virtual AuthState GetState() const = 0;
        virtual uint64_t GetMappedBase() const = 0;
        virtual size_t GetMappedSize() const = 0;
        virtual void* GetDeviceHandle() const = 0;
    };
}
