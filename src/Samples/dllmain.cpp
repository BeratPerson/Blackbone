#include <Windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#pragma comment(linker, "/ENTRY:DllMain")

typedef struct _MY_PEB_LDR_DATA {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} MY_PEB_LDR_DATA, *PMY_PEB_LDR_DATA;

typedef struct _MY_LDR_DATA_TABLE_ENTRY {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG Flags;
    WORD LoadCount;
    WORD TlsIndex;
} MY_LDR_DATA_TABLE_ENTRY, *PMY_LDR_DATA_TABLE_ENTRY;

// ============================================
// 1. SELF PEB UNLINK (Local process PEB)
// ============================================
void SelfUnlinkPEB(HMODULE hSelf)
{
    PPEB peb = (PPEB)__readfsdword(0x30); // x86 PEB
    if (!peb) return;
    PMY_PEB_LDR_DATA ldr = (PMY_PEB_LDR_DATA)peb->Ldr;
    if (!ldr) return;

    wchar_t myPath[MAX_PATH];
    GetModuleFileNameW(hSelf, myPath, MAX_PATH);
    wchar_t* myName = wcsrchr(myPath, L'\\');
    if (myName) myName++; else myName = myPath;
    size_t myNameLen = wcslen(myName);

    PLIST_ENTRY lists[3] = {
        &ldr->InLoadOrderModuleList,
        &ldr->InMemoryOrderModuleList,
        &ldr->InInitializationOrderModuleList
    };

    for (int i = 0; i < 3; i++) {
        PLIST_ENTRY head = lists[i];
        PLIST_ENTRY cur = head->Flink;
        while (cur && cur != head) {
            PMY_LDR_DATA_TABLE_ENTRY entry = nullptr;
            if (i == 0) entry = CONTAINING_RECORD(cur, MY_LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
            else if (i == 1) entry = CONTAINING_RECORD(cur, MY_LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
            else entry = CONTAINING_RECORD(cur, MY_LDR_DATA_TABLE_ENTRY, InInitializationOrderLinks);

            if (entry->BaseDllName.Buffer && entry->BaseDllName.Length > 0) {
                size_t entryNameLen = entry->BaseDllName.Length / sizeof(wchar_t);
                if (myNameLen == entryNameLen && _wcsnicmp(myName, entry->BaseDllName.Buffer, entryNameLen) == 0) {
                    // Unlink
                    if (cur->Blink && cur->Flink) {
                        cur->Blink->Flink = cur->Flink;
                        cur->Flink->Blink = cur->Blink;
                        cur->Flink = cur;
                        cur->Blink = cur;
                    }
                    break;
                }
            }
            cur = cur->Flink;
        }
    }
}

// ============================================
// 2. PE HEADER WIPE (Kernel/VAD taramasindan gizle)
// ============================================
void WipePEHeader(HMODULE hSelf)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hSelf;
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return;

    PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hSelf + dos->e_lfanew);
    SIZE_T headerSize = nt->OptionalHeader.SizeOfHeaders;

    DWORD oldProt = 0;
    if (VirtualProtect(hSelf, headerSize, PAGE_EXECUTE_READWRITE, &oldProt)) {
        memset(hSelf, 0, headerSize);
        VirtualProtect(hSelf, headerSize, oldProt, &oldProt);
    }
}

// ============================================
// 3. WORKER THREAD (Cheat Logic / HUD)
// ============================================
DWORD WINAPI WorkerThread(LPVOID lpParam)
{
    // Knight Online karakter verisi okuma
    const uintptr_t PTR_CHR = 0x011319E0;

    while (true) {
        uintptr_t pChar = 0;
        __try {
            pChar = *(uintptr_t*)PTR_CHR;
            if (pChar) {
                int32_t hp = *(int32_t*)(pChar + 0x5F4);
                int32_t maxHp = *(int32_t*)(pChar + 0x5F0);
                (void)hp;
                (void)maxHp;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            // Hata toleransi
        }
        Sleep(100);
    }
    return 0;
}

// ============================================
// DLL MAIN
// ============================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Loader Lock'tan kaÃ§mak iÃ§in async thread baÅŸlat
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
            HMODULE hSelf = (HMODULE)param;
            Sleep(2500); // Loader lock ve init bitsin

            SelfUnlinkPEB(hSelf);
            WipePEHeader(hSelf);

            // ArtÄ±k XignCode3 user-mode PEB scan'de gÃ¶remez
            WorkerThread(hSelf);
            return 0;
        }, hModule, 0, nullptr);
    }
    return TRUE;
}
