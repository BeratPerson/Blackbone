#include "ManualMapXign.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <tlhelp32.h>
#include <winternl.h>

using namespace blackbone;

static FnLogCallback g_logCb = nullptr;

void SetXignLogCallback(FnLogCallback cb)
{
    g_logCb = cb;
}

static void InternalLog(const wchar_t* fmt, ...)
{
    if (!g_logCb) return;
    wchar_t buf[2048] = {};
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    g_logCb(L"%s", buf);
}

DWORD FindProcessByName(const wchar_t* procName)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD foundPid = 0;

    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, procName) == 0)
            {
                foundPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return foundPid;
}

bool WaitForModuleXign(Process& proc, const wchar_t* modName, DWORD timeoutMs)
{
    auto start = std::chrono::steady_clock::now();
    DWORD lastLoggedSec = 0;

    while (true)
    {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (proc.modules().GetModule(modName))
        {
            auto mod = proc.modules().GetModule(modName);
            InternalLog(L"[+] '%s' modulu bellekte yakalandi! Taban Adresi: 0x%p, Boyut: 0x%X (%lld ms sonra)", 
                modName, (void*)mod->baseAddress, mod->size, elapsedMs);
            return true;
        }

        if (elapsedMs > (long long)timeoutMs)
        {
            InternalLog(L"[!] '%s' modulu icin zaman asimi doldu (%lu ms).", modName, timeoutMs);
            return false;
        }

        DWORD currentSec = static_cast<DWORD>(elapsedMs / 1000);
        if (currentSec != lastLoggedSec && currentSec > 0 && (currentSec % 2 == 0))
        {
            lastLoggedSec = currentSec;
            InternalLog(L"[*] '%s' bekleniyor... (Gecen sure: %lu sn / %lu sn)", modName, currentSec, timeoutMs / 1000);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

bool SuspendProcessXign(Process& proc)
{
    static auto NtSuspendProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess");
    if (!NtSuspendProcess)
    {
        InternalLog(L"[-] NtSuspendProcess adresi ntdll.dll icinde bulunamadi!");
        return false;
    }

    NTSTATUS status = NtSuspendProcess(proc.core().handle());
    if (NT_SUCCESS(status))
    {
        InternalLog(L"[+] NtSuspendProcess basarili (STATUS: 0x%08X). Surec donduruldu.", status);
        return true;
    }
    else
    {
        InternalLog(L"[-] NtSuspendProcess basarisiz (STATUS: 0x%08X)!", status);
        return false;
    }
}

bool ResumeProcessXign(Process& proc)
{
    static auto NtResumeProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess");
    if (!NtResumeProcess)
    {
        InternalLog(L"[-] NtResumeProcess adresi ntdll.dll icinde bulunamadi!");
        return false;
    }

    NTSTATUS status = NtResumeProcess(proc.core().handle());
    if (NT_SUCCESS(status))
    {
        InternalLog(L"[+] NtResumeProcess basarili (STATUS: 0x%08X). Surec calismaya devam ediyor.", status);
        return true;
    }
    else
    {
        InternalLog(L"[-] NtResumeProcess basarisiz (STATUS: 0x%08X)!", status);
        return false;
    }
}

uintptr_t ManualMapDll(Process& proc, const wchar_t* dllPath)
{
    InternalLog(L"[*] ManualMap: Haritalama baslatiliyor (DLL: %s)...", dllPath);

    // Try loading the BlackBone kernel driver (HideVAD requires it).
    // Driver is x64-only; on a WOW64 process this may still succeed on x64 OS.
    eLoadFlags flags = static_cast<eLoadFlags>(
        static_cast<int>(eLoadFlags::WipeHeader) |
        static_cast<int>(eLoadFlags::ManualImports));

    {
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        std::wstring drvDir = slash ? std::wstring(exePath, slash + 1) : L"";

        const wchar_t* drvNames[] = {
            L"BlackBoneDrv10.sys", L"BlackBoneDrv81.sys",
            L"BlackBoneDrv8.sys",  L"BlackBoneDrv7.sys"
        };
        for (auto* n : drvNames)
        {
            std::wstring fullPath = drvDir + n;
            DWORD attr = GetFileAttributesW(fullPath.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES)
            {
                InternalLog(L"[*] Surucü deneniyor: %s", fullPath.c_str());
                if (NT_SUCCESS(Driver().EnsureLoaded(fullPath)))
                {
                    flags = static_cast<eLoadFlags>(
                        static_cast<int>(flags) | static_cast<int>(eLoadFlags::HideVAD));
                    InternalLog(L"[+] BlackBone surucusu yuklendi — HideVAD AKTIF.");
                    break;
                }
            }
        }
        if (!(static_cast<int>(flags) & static_cast<int>(eLoadFlags::HideVAD)))
            InternalLog(L"[!] BlackBone surucusu yuklenemedi — HideVAD devre disi (WipeHeader modunda devam).");
    }

    auto image = proc.mmap().MapImage(dllPath, flags);

    if (!image)
    {
        InternalLog(L"[-] MapImage basarisiz! NTSTATUS: 0x%08X", image.status);
        return 0;
    }

    auto modInfo = image.result();
    uintptr_t base = static_cast<uintptr_t>(modInfo->baseAddress);
    InternalLog(L"[+] Manual Map tamamlandi. Base: 0x%p, Boyut: 0x%X", (void*)base, modInfo->size);

    return base;
}

bool RemoteUnlinkModule(Process& proc, uintptr_t modBase, const wchar_t* nameTag)
{
    if (!modBase)
    {
        InternalLog(L"[!] RemoteUnlinkModule: Gecersiz base address (0x0).");
        return false;
    }

    const DWORD PebLdrOffset = 0x0C; // x86 PEB+0x0C -> Ldr
    struct LdrCtx {
        DWORD LdrListOffset;
        DWORD EntryListOffset;
        const wchar_t* Name;
    };
    LdrCtx lists[] = {
        { 0x0C, 0x00, L"InLoadOrderModuleList" },
        { 0x14, 0x08, L"InMemoryOrderModuleList" },
        { 0x1C, 0x10, L"InInitializationOrderModuleList" }
    };

    PROCESS_BASIC_INFORMATION pbi = {};
    ULONG rl = 0;
    static auto NtQIP = (NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");

    if (!NtQIP || NtQIP(proc.core().handle(), 0, &pbi, sizeof(pbi), &rl) != 0 || !pbi.PebBaseAddress)
    {
        InternalLog(L"[-] RemoteUnlinkModule: NtQueryInformationProcess basarisiz.");
        return false;
    }

    uintptr_t peb = (uintptr_t)pbi.PebBaseAddress;
    uintptr_t ldrPtr = 0;
    SIZE_T rd = 0;

    if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(peb + PebLdrOffset), &ldrPtr, sizeof(ldrPtr), &rd) || !ldrPtr)
    {
        InternalLog(L"[-] RemoteUnlinkModule: PEB_LDR_DATA okunamadi.");
        return false;
    }

    InternalLog(L"[*] Remote PEB taraniyor (PEB: 0x%p, Ldr: 0x%p, Hedef Base: 0x%p)...", 
        (void*)peb, (void*)ldrPtr, (void*)modBase);

    bool any = false;
    for (const auto& ctx : lists)
    {
        uintptr_t listHead = ldrPtr + ctx.LdrListOffset;
        uintptr_t current = 0;

        if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)listHead, &current, sizeof(current), &rd) || !current)
            continue;

        int guard = 0;
        while (current != listHead && current != 0 && guard++ < 512)
        {
            uintptr_t entryBase = current - ctx.EntryListOffset;

            uintptr_t readBase = 0;
            // 0x18 offset x86 LDR_DATA_TABLE_ENTRY -> DllBase
            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(entryBase + 0x18), &readBase, sizeof(readBase), &rd))
                break;

            if (readBase == modBase)
            {
                uintptr_t flink = 0, blink = 0;
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &flink, sizeof(flink), &rd);
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 4), &blink, sizeof(blink), &rd);

                SIZE_T bw = 0;
                WriteProcessMemory(proc.core().handle(), (LPVOID)(blink + 0), &flink, sizeof(flink), &bw);
                WriteProcessMemory(proc.core().handle(), (LPVOID)(flink + 4), &blink, sizeof(blink), &bw);

                DWORD zero[2] = { 0, 0 };
                WriteProcessMemory(proc.core().handle(), (LPVOID)current, zero, sizeof(zero), &bw);

                any = true;
                InternalLog(L"    [+] %s listesinden '%s' (0x%p) basariyla unlinked.", ctx.Name, nameTag, (void*)modBase);
                break;
            }

            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &current, sizeof(current), &rd))
                break;
        }
    }
    return any;
}
