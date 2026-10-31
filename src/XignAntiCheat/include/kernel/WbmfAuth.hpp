#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Xign
{
    struct NtUnicodeString
    {
        uint16_t Length;
        uint16_t MaximumLength;
        wchar_t* Buffer;
    };

    struct NtObjectAttributes
    {
        uint32_t Length;
        void* RootDirectory;
        NtUnicodeString* ObjectName;
        uint32_t Attributes;
        void* SecurityDescriptor;
        void* SecurityQualityOfService;
    };

    struct NtIoStatusBlock
    {
        union { uint32_t Status; void* Pointer; };
        uintptr_t Information;
    };

    class WbmfAuth
    {
    public:
        static bool MapWbmfModule(const std::vector<uint8_t>& dllBytes, uint64_t& outBase, size_t& outSize);
        static bool VerifyWbmfMagic(uint64_t base, size_t size);
        static bool InstallTrampoline(uint64_t base, uint32_t offset, uint64_t target, bool x64);
        static std::vector<uint8_t> BuildEaBuffer(uint64_t peBase);
        static std::wstring NormaliseDevice(const std::wstring& name);
        static bool OpenDeviceHandle(const std::wstring& deviceName,
                                     const std::vector<uint8_t>& eaBuffer,
                                     void** outHandle);
        static bool ExecuteWbmfTrampoline(uint64_t mappedBase, uint32_t trampolineOffset);
    };
}