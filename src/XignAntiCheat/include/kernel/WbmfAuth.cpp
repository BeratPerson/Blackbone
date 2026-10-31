#include "WbmfAuth.hpp"
#include "XignDefs.hpp"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>

// ntdll imports
extern "C" __declspec(dllimport) uint32_t __stdcall NtCreateFile(
    void** FileHandle,
    uint32_t DesiredAccess,
    NtObjectAttributes* ObjectAttributes,
    NtIoStatusBlock* IoStatusBlock,
    void* AllocationSize,
    uint32_t FileAttributes,
    uint32_t ShareAccess,
    uint32_t CreateDisposition,
    uint32_t CreateOptions,
    void* EaBuffer,
    uint32_t EaLength
);

extern "C" __declspec(dllimport) uint32_t __stdcall NtClose(void* Handle);

constexpr uint32_t OBJ_CASE_INSENSITIVE = 0x00000040;
constexpr uint32_t FILE_OPEN              = 0x00000001;
constexpr uint32_t FILE_NON_DIRECTORY_FILE = 0x00000040;
constexpr uint32_t FILE_SYNCHRONIZE_IO_NONALERT = 0x00040000;
#endif

namespace Xign
{
    bool WbmfAuth::MapWbmfModule(const std::vector<uint8_t>& dllBytes, uint64_t& outBase, size_t& outSize)
    {
        if (dllBytes.size() < 0x40) return false;
        uint32_t lfanew = *reinterpret_cast<const uint32_t*>(&dllBytes[0x3c]);
        if (lfanew + 0x60 > dllBytes.size()) return false;
        if (std::memcmp(&dllBytes[lfanew], "PE\0\0", 4) != 0) return false;

        uint32_t imgBase = *reinterpret_cast<const uint32_t*>(&dllBytes[lfanew + 52]);
        uint32_t soi     = *reinterpret_cast<const uint32_t*>(&dllBytes[lfanew + 80]);
        if (soi == 0 || soi > 0x10000000) return false;

#ifdef _WIN32
        void* base = VirtualAlloc(
            reinterpret_cast<void*>(static_cast<uintptr_t>(imgBase)),
            soi, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!base) {
            base = VirtualAlloc(nullptr, soi, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!base) return false;
        }
        std::memcpy(base, dllBytes.data(), (std::min)(static_cast<size_t>(dllBytes.size()), static_cast<size_t>(soi)));

        DWORD oldProtect = 0;
        VirtualProtect(base, soi, PAGE_EXECUTE_READ, &oldProtect);

        outBase = reinterpret_cast<uint64_t>(base);
        outSize = soi;
        return true;
#else
        (void)outBase; (void)outSize;
        return false;
#endif
    }

    bool WbmfAuth::VerifyWbmfMagic(uint64_t base, size_t size)
    {
        if (WBMF_MAGIC_OFFSET + 4 > size) return false;
        return std::memcmp(reinterpret_cast<const void*>(base + WBMF_MAGIC_OFFSET), "WBMF", 4) == 0;
    }

    bool WbmfAuth::InstallTrampoline(uint64_t base, uint32_t offset, uint64_t target, bool x64)
    {
        uint8_t* addr = reinterpret_cast<uint8_t*>(base + offset);
        DWORD oldProt = 0;
#ifdef _WIN32
        if (!VirtualProtect(addr, 14, PAGE_EXECUTE_READWRITE, &oldProt))
            return false;
#endif
        if (!x64) {
            // x86: push imm32 ; ret  (6 bytes)
            uint8_t tramp[6] = { 0x68, 0,0,0,0, 0xC3 };
            *reinterpret_cast<uint32_t*>(&tramp[1]) = static_cast<uint32_t>(target);
            std::memcpy(addr, tramp, sizeof(tramp));
        } else {
            // x64: FF 25 00 00 00 00 <abs64>  (14 bytes)
            uint8_t tramp[14] = { 0xFF, 0x25, 0,0,0,0, 0,0,0,0,0,0,0,0 };
            *reinterpret_cast<uint64_t*>(&tramp[6]) = target;
            std::memcpy(addr, tramp, sizeof(tramp));
        }
#ifdef _WIN32
        VirtualProtect(addr, 14, oldProt, &oldProt);
#endif
        return true;
    }

    std::vector<uint8_t> WbmfAuth::BuildEaBuffer(uint64_t peBase)
    {
        const char eaName[] = "WBCT";
        uint8_t nameLen = static_cast<uint8_t>(sizeof(eaName)); // includes null

        size_t totalSize = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint16_t)
                         + nameLen + WBCT_VAL;
        // Align to 4
        std::vector<uint8_t> buf(totalSize, 0);

        size_t pos = 0;
        *reinterpret_cast<uint32_t*>(&buf[pos]) = 0; pos += 4;           // NextEntryOffset
        buf[pos++] = 0;                                                 // Flags
        buf[pos++] = nameLen;                                           // EaNameLength
        *reinterpret_cast<uint16_t*>(&buf[pos]) = static_cast<uint16_t>(WBCT_VAL); pos += 2; // EaValueLength

        std::memcpy(&buf[pos], eaName, nameLen); pos += nameLen;

        uint8_t* valPtr = &buf[pos];
        std::memcpy(&valPtr[0], "WBCT", 4);
        *reinterpret_cast<uint16_t*>(&valPtr[4]) = static_cast<uint16_t>(WBCT_VAL);
        *reinterpret_cast<uint16_t*>(&valPtr[6]) = 2; // version flag
        std::memcpy(&valPtr[16], &peBase, sizeof(peBase));
        std::memcpy(&valPtr[24], &peBase, sizeof(peBase));

        return buf;
    }

    std::wstring WbmfAuth::NormaliseDevice(const std::wstring& name)
    {
        if (name.find(L"\\Device\\") == 0 || name.find(L"\\\\.\\") == 0 || name.find(L"\\??\\") == 0)
            return name;
        return std::wstring(L"\\Device\\") + name;
    }

    bool WbmfAuth::OpenDeviceHandle(const std::wstring& deviceName,
                                    const std::vector<uint8_t>& eaBuffer,
                                    void** outHandle)
    {
#ifdef _WIN32
        if (!outHandle) return false;
        *outHandle = nullptr;

        std::wstring devPath = NormaliseDevice(deviceName);

        NtUnicodeString uniName{};
        uniName.Buffer = const_cast<wchar_t*>(devPath.c_str());
        uniName.Length = static_cast<uint16_t>(devPath.length() * sizeof(wchar_t));
        uniName.MaximumLength = uniName.Length + sizeof(wchar_t);

        NtObjectAttributes objAttr{};
        objAttr.Length = sizeof(NtObjectAttributes);
        objAttr.RootDirectory = nullptr;
        objAttr.ObjectName = &uniName;
        objAttr.Attributes = OBJ_CASE_INSENSITIVE;
        objAttr.SecurityDescriptor = nullptr;
        objAttr.SecurityQualityOfService = nullptr;

        NtIoStatusBlock ioStatus{};
        void* hFile = nullptr;

        uint32_t status = NtCreateFile(
            &hFile,
            0xC0000000 | 0x00100000 | 0x00100000, // GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE
            &objAttr,
            &ioStatus,
            nullptr,
            FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            FILE_OPEN,
            FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONIZE_IO_NONALERT,
            const_cast<void*>(static_cast<const void*>(eaBuffer.data())),
            static_cast<uint32_t>(eaBuffer.size())
        );

        if (status != 0 || !hFile || hFile == reinterpret_cast<void*>(-1)) {
            std::cerr << "[!] NtCreateFile failed, status=0x" << std::hex << status << '\n';
            return false;
        }

        *outHandle = hFile;
        return true;
#else
        return false;
#endif
    }

    bool WbmfAuth::ExecuteWbmfTrampoline(uint64_t mappedBase, uint32_t trampolineOffset)
    {
#ifdef _WIN32
        void* threadAddr = reinterpret_cast<void*>(mappedBase + trampolineOffset);
        DWORD tid = 0;
        HANDLE hThread = CreateThread(nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(threadAddr),
            nullptr, 0, &tid);
        if (!hThread) return false;

        WaitForSingleObject(hThread, 8000);
        CloseHandle(hThread);
        return true;
#else
        return false;
#endif
    }
}