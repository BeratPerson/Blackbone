#include "ManualMapXign.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <winternl.h>

using namespace blackbone;

bool WaitForModuleXign(Process& proc, const wchar_t* modName, DWORD timeoutMs)
{
    auto start = std::chrono::steady_clock::now();
    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > (long long)timeoutMs) return false;

        if (proc.modules().GetModule(modName)) return true;

        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }
}

bool SuspendProcessXign(Process& proc)
{
    static auto NtSuspendProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess");
    if (NtSuspendProcess) {
        return NT_SUCCESS(NtSuspendProcess(proc.core().handle()));
    }
    return false;
}

bool ResumeProcessXign(Process& proc)
{
    static auto NtResumeProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess");
    if (NtResumeProcess) {
        return NT_SUCCESS(NtResumeProcess(proc.core().handle()));
    }
    return false;
}

uintptr_t ManualMapDll(Process& proc, const wchar_t* dllPath)
{
    // Blackbone ManualMap: PE header sil, VAD gizle, thread hijacking ile DllMain calistir
    auto image = proc.mmap().MapImage(dllPath,
        eLoadFlags::WipeHeader |       // PE header bellekten silinir
        eLoadFlags::HideVAD |          // NtQueryVirtualMemory ile gorunmez
        eLoadFlags::NoThreads          // Yeni thread yerine thread hijacking kullan
    );

    if (!image) {
        std::wcerr << L"[-] MapImage basarisiz. Status: 0x" << std::hex << image.status << std::dec << std::endl;
        return 0;
    }

    uintptr_t base = static_cast<uintptr_t>((*image)->baseAddress);
    std::wcout << L"[+] Image mapped at 0x" << std::hex << base << std::dec << std::endl;

    return base;
}

bool RemoteUnlinkModule(Process& proc, uintptr_t modBase, const wchar_t* nameTag)
{
    if (!modBase) return false;

    // Win10/11 x86 PEB/LDR offsets
    const DWORD PebLdrOffset = 0x0C; // PEB+0x0C -> Ldr (x86)
    const struct {
        DWORD LdrListOffset;   // PEB_LDR_DATA icindeki LIST_ENTRY
        DWORD EntryListOffset; // LDR_DATA_TABLE_ENTRY icindeki LIST_ENTRY
        const wchar_t* Name;
    } lists[] = {
        { 0x0C, 0x00, L"InLoadOrder" },
        { 0x14, 0x08, L"InMemoryOrder" },
        { 0x1C, 0x10, L"InInitOrder" }
    };

    // Remote PEB al
    PROCESS_BASIC_INFORMATION pbi = {};
    ULONG rl = 0;
    static auto NtQIP = (NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");

    if (!NtQIP || NtQIP(proc.core().handle(), 0, &pbi, sizeof(pbi), &rl) != 0 || !pbi.PebBaseAddress)
        return false;

    uintptr_t peb = (uintptr_t)pbi.PebBaseAddress;
    uintptr_t ldrPtr = 0;
    SIZE_T rd = 0;

    if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(peb + PebLdrOffset), &ldrPtr, sizeof(ldrPtr), &rd) || !ldrPtr)
        return false;

    bool any = false;
    for (const auto& ctx : lists) {
        uintptr_t listHead = ldrPtr + ctx.LdrListOffset;
        uintptr_t current = 0;

        if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)listHead, &current, sizeof(current), &rd) || !current)
            continue;

        int guard = 0;
        while (current != listHead && current != 0 && guard++ < 512) {
            uintptr_t entryBase = current - ctx.EntryListOffset;

            uintptr_t readBase = 0;
            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(entryBase + 0x18), &readBase, sizeof(readBase), &rd))
                break;

            if (readBase == modBase) {
                uintptr_t flink = 0, blink = 0;
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &flink, sizeof(flink), &rd);
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 4), &blink, sizeof(blink), &rd);

                SIZE_T bw = 0;
                WriteProcessMemory(proc.core().handle(), (LPVOID)(blink + 0), &flink, sizeof(flink), &bw);
                WriteProcessMemory(proc.core().handle(), (LPVOID)(flink + 4), &blink, sizeof(blink), &bw);

                DWORD zero[2] = { 0, 0 };
                WriteProcessMemory(proc.core().handle(), (LPVOID)current, zero, sizeof(zero), &bw);

                any = true;
                std::wcout << L"    [+] " << nameTag << L" <- " << ctx.Name << L" listesinden cikarildi." << std::endl;
                break;
            }

            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &current, sizeof(current), &rd))
                break;
        }
    }
    return any;
}
