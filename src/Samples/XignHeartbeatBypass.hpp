#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

// XignCode3 x3.xem Heartbeat / Validation Bypass
// Injects into the game process; patches x3.xem validation handlers
// so heartbeat and integrity checks always return success.

namespace XignBypass
{
    using LogFn = std::function<void(const wchar_t*)>;

    struct PatchRecord
    {
        std::string   name;
        uintptr_t     address;
        bool          success;
        std::string   detail;
        uint8_t       original[8];
        size_t        patchLen;
    };

    class HeartbeatBypass
    {
    public:
        explicit HeartbeatBypass(LogFn log = nullptr);

        // Find x3.xem, patch all validation handlers. Returns patch results.
        std::vector<PatchRecord> Install();

        // Remove patches (restore original bytes).
        void Uninstall();

        bool IsInstalled() const { return m_installed; }

    private:
        void Log(const wchar_t* fmt, ...) const;

        // Locate x3.xem in the current process (retries up to timeoutMs).
        uintptr_t WaitForX3(DWORD timeoutMs = 8000);

        // Patch arbitrary bytes at base+rva (patchLen <= 8).
        PatchRecord PatchHandler(uintptr_t base, uint32_t rva, const char* name,
                                 const uint8_t* patchBytes, size_t patchLen);

        // Verify the module header is valid PE32.
        bool ValidatePE(uintptr_t base);

        LogFn  m_log;
        bool   m_installed = false;
        std::vector<PatchRecord> m_patches;
    };

} // namespace XignBypass
