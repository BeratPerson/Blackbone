#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstdio>

// ========== Ileri Bildirimler ==========
void WorkerThread(HMODULE hSelf);

// ========== NT Yapilari ==========
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _LDR_DATA_TABLE_ENTRY_FULL {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID      DllBase;
    PVOID      EntryPoint;
    ULONG      SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    ULONG      Flags;
    USHORT     LoadCount;
    USHORT     TlsIndex;
    LIST_ENTRY HashLinks;
    ULONG      TimeDateStamp;
} LDR_DATA_TABLE_ENTRY_FULL, * PLDR_DATA_TABLE_ENTRY_FULL;

// ========== SelfUnlinkPEB (3 Liste Guvenli Temizlik) ==========
static void SelfUnlinkPEB(HMODULE hSelf)
{
    if (!hSelf) return;

#ifdef _WIN64
    PPEB peb = (PPEB)__readgsqword(0x60);
#else
    PPEB peb = (PPEB)__readfsdword(0x30);
#endif

    if (!peb || !peb->Ldr) return;

    PLIST_ENTRY listHead = &peb->Ldr->InLoadOrderModuleList;
    PLIST_ENTRY entry = listHead->Flink;

    while (entry != listHead)
    {
        PLDR_DATA_TABLE_ENTRY_FULL mod = (PLDR_DATA_TABLE_ENTRY_FULL)entry;

        // InLoadOrderModuleList uzerinden gezerken InLoadOrderLinks'i temsil eder
        // Ancak biz struct baslangicini bulmamiz gerekir. Winternl.h'de LDR_DATA_TABLE_ENTRY
        // farkli tanimli olabilir; biz basit pointer aritmetigiyle buluyoruz.
        // Dogrudan Flink uzerinden modulun base adresine eriselim:
        if (mod->DllBase == hSelf)
        {
            // 1) InLoadOrderModuleList
            if (mod->InLoadOrderLinks.Flink && mod->InLoadOrderLinks.Blink)
            {
                mod->InLoadOrderLinks.Blink->Flink = mod->InLoadOrderLinks.Flink;
                mod->InLoadOrderLinks.Flink->Blink = mod->InLoadOrderLinks.Blink;
                mod->InLoadOrderLinks.Flink = mod->InLoadOrderLinks.Blink = nullptr;
            }

            // 2) InMemoryOrderModuleList
            if (mod->InMemoryOrderLinks.Flink && mod->InMemoryOrderLinks.Blink)
            {
                mod->InMemoryOrderLinks.Blink->Flink = mod->InMemoryOrderLinks.Flink;
                mod->InMemoryOrderLinks.Flink->Blink = mod->InMemoryOrderLinks.Blink;
                mod->InMemoryOrderLinks.Flink = mod->InMemoryOrderLinks.Blink = nullptr;
            }

            // 3) InInitializationOrderModuleList
            if (mod->InInitializationOrderLinks.Flink && mod->InInitializationOrderLinks.Blink)
            {
                mod->InInitializationOrderLinks.Blink->Flink = mod->InInitializationOrderLinks.Flink;
                mod->InInitializationOrderLinks.Flink->Blink = mod->InInitializationOrderLinks.Blink;
                mod->InInitializationOrderLinks.Flink = mod->InInitializationOrderLinks.Blink = nullptr;
            }
            break;
        }
        entry = entry->Flink;
    }
}

// ========== WipePEHeader (Guvenli) ==========
static void WipePEHeader(HMODULE hSelf)
{
    if (!hSelf) return;

    __try
    {
        PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hSelf;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;

        PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hSelf + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return;

        DWORD size = nt->OptionalHeader.SizeOfHeaders;
        if (!size) return;

        DWORD oldProt = 0;
        if (VirtualProtect(hSelf, size, PAGE_EXECUTE_READWRITE, &oldProt))
        {
            RtlZeroMemory(hSelf, size);
            VirtualProtect(hSelf, size, oldProt, &oldProt);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // PE Header silinirken hata olursa sessizce gec
    }
}

// ========== DllMain ==========
extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    (void)lpReserved;

    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(nullptr, 0,
            [](LPVOID param) -> DWORD {
                HMODULE hSelf = static_cast<HMODULE>(param);

                // XignCode'un init + hash check dongusunu gecmesini bekle
                Sleep(3500);

                __try
                {
                    SelfUnlinkPEB(hSelf);
                    WipePEHeader(hSelf);
                    WorkerThread(hSelf);   // Senin kodun
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Exception yut: Oyunun kapanmasini engelle
                }

                return 0;
            },
            hModule, 0, nullptr);

        if (hThread) CloseHandle(hThread);
    }
    return TRUE;
}