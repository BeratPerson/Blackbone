// dllmain.cpp  —  berat.dll (injected payload)
// Amac: x3.xem icine manual-map ile enjekte edilir.
// Gorevler:
//   1. PEB'den kendini gizle (SelfUnlinkPEB)
//   2. PE header'i sil (WipePEHeader)
//   3. x3.xem validation handler'larini patch et (HeartbeatBypass)
//   4. Oyun calisirken stabilite testleri (WorkerThread)

#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <functional>

#include "XignHeartbeatBypass.hpp"
#include "MemoryReaderUI.hpp"

// ======================================================================
// NT Yapi Tanimlari
// ======================================================================
typedef struct _PEB_LDR_DATA_FULL {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID EntryInProgress;
    BOOLEAN ShutdownInProgress;
    HANDLE ShutdownThreadId;
} PEB_LDR_DATA_FULL, * PPEB_LDR_DATA_FULL;

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

// ======================================================================
// Loglama (OutputDebugString)
// ======================================================================
static void DbgLog(const wchar_t* fmt, ...)
{
    wchar_t buf[2048] = {};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(L"[berat.dll] ");
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}

// ======================================================================
// SelfUnlinkPEB — 3 listeden cikar
// ======================================================================
static void SelfUnlinkPEB(HMODULE hSelf)
{
    if (!hSelf) return;

#ifdef _WIN64
    PPEB peb = (PPEB)__readgsqword(0x60);
#else
    PPEB peb = (PPEB)__readfsdword(0x30);
#endif
    if (!peb || !peb->Ldr) return;

    PPEB_LDR_DATA_FULL ldr = (PPEB_LDR_DATA_FULL)peb->Ldr;
    PLIST_ENTRY listHead = &ldr->InLoadOrderModuleList;
    PLIST_ENTRY entry = listHead->Flink;

    while (entry != listHead)
    {
        PLDR_DATA_TABLE_ENTRY_FULL mod = (PLDR_DATA_TABLE_ENTRY_FULL)entry;
        entry = entry->Flink;

        if (mod->DllBase == hSelf)
        {
            auto unlink = [](LIST_ENTRY& e) {
                if (e.Flink && e.Blink) {
                    e.Blink->Flink = e.Flink;
                    e.Flink->Blink = e.Blink;
                    e.Flink = e.Blink = nullptr;
                }
            };
            unlink(mod->InLoadOrderLinks);
            unlink(mod->InMemoryOrderLinks);
            unlink(mod->InInitializationOrderLinks);
            DbgLog(L"PEB unlink tamamlandi.");
            break;
        }
    }
}

// ======================================================================
// WipePEHeader — PE header byte'larini sifirla
// ======================================================================
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
            DbgLog(L"PE header temizlendi (%lu byte).", size);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DbgLog(L"PE header silme sirasinda exception yutuldu.");
    }
}

// ======================================================================
// Worker Thread — ana bypass mantigi
// ======================================================================
static void WorkerThread(HMODULE hSelf)
{
    DbgLog(L"============================================================");
    DbgLog(L"berat.dll Worker Thread basladi.");
    DbgLog(L"============================================================");

    // --- Adim 1: Gizleme ---
    DbgLog(L"[1/3] PEB unlink + PE header wipe...");
    SelfUnlinkPEB(hSelf);
    WipePEHeader(hSelf);
    DbgLog(L"[1/3] Gizleme tamamlandi.");

    // --- Adim 2: x3.xem core init bekleniyor (g_pCoreInterface non-null olana kadar) ---
    DbgLog(L"[2/3] x3.xem g_pCoreInterface bekleniyor (maks. 6 sn)...");
    {
        HMODULE hX3 = nullptr;
        for (int i = 0; i < 60 && !hX3; ++i)          // maks. 3s for module
        {
            hX3 = GetModuleHandleW(L"x3.xem");
            if (!hX3) Sleep(50);
        }
        if (hX3)
        {
            // RVA 0x2A904C = g_pCoreInterface (IXigncodeCore COM pointer)
            volatile DWORD* pCore = reinterpret_cast<volatile DWORD*>(
                reinterpret_cast<BYTE*>(hX3) + 0x2A904Cu);
            for (int i = 0; i < 120; ++i)              // maks. 6s for init
            {
                if (*pCore != 0) break;
                Sleep(50);
            }
            DbgLog(L"[2/3] g_pCoreInterface = 0x%08X", *pCore);
        }
        else
        {
            DbgLog(L"[2/3] x3.xem yüklenemedi, kisa bekleme ile devam ediliyor...");
            Sleep(500);
        }
    }

    // --- Adim 3: Heartbeat Bypass ---
    DbgLog(L"[3/3] Heartbeat bypass kuruluyor...");

    XignBypass::HeartbeatBypass bypass([](const wchar_t* msg) {
        DbgLog(L"%s", msg);
    });

    auto results = bypass.Install();

    // Sonuclari logla
    int ok = 0, fail = 0;
    for (const auto& r : results)
    {
        if (r.success) ++ok; else ++fail;
        DbgLog(L"  %s  [%S] @0x%p  %S",
            r.success ? L"[OK]  " : L"[FAIL]",
            r.name.c_str(),
            (void*)r.address,
            r.detail.c_str());
    }

    DbgLog(L"[3/3] Bypass sonucu: %d basarili / %d basarisiz", ok, fail);

    if (bypass.IsInstalled())
    {
        DbgLog(L"============================================================");
        DbgLog(L"[OK] BYPASS AKTIF - Heartbeat/Validation patched.");
        DbgLog(L"============================================================");

        // Kullaniciya goster (test — production'da kaldirilabilir)
        MessageBoxW(nullptr,
            L"berat.dll yuklendi!\n\nHeartbeat bypass AKTIF.\nx3.xem validation handler'lari patched.",
            L"berat.dll - Bypass Aktif",
            MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    }
    else
    {
        DbgLog(L"============================================================");
        DbgLog(L"[WARN] Heartbeat bypass kurulmadi - oyun stabiliteligi belirsiz.");
        DbgLog(L"============================================================");

        MessageBoxW(nullptr,
            L"berat.dll yuklendi!\n\nHeartbeat bypass BASARISIZ oldu.\nx3.xem bulunamadi veya patch uygulanamadi.",
            L"berat.dll - Uyari",
            MB_OK | MB_ICONWARNING | MB_SETFOREGROUND);
    }

    // Memory Reader UI ac (kendi thread'inde calisir, mesaj dongusu icerir)
    DbgLog(L"[4/4] Memory Reader UI aciliyor...");
    MemUI::CreateMemoryReaderWindow();
    DbgLog(L"[4/4] Memory Reader UI thread baslatildi.");

    // Bypass aktif kaldigi surece thread yasatilmasi gerekmiyor.
    // Patch'ler kalici (VirtualProtect ile yazildi) - thread burada bitebilir.
    DbgLog(L"Worker thread sonlandirildi. Bypass pasif hafizada aktif kalir.");
}

// ======================================================================
// DllMain
// ======================================================================
extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    (void)lpReserved;

    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // XignCode'un module scan'inden once hizla thread ac
        HANDLE hThread = CreateThread(nullptr, 0,
            [](LPVOID param) -> DWORD {
                HMODULE hSelf = static_cast<HMODULE>(param);
                __try { WorkerThread(hSelf); }
                __except (EXCEPTION_EXECUTE_HANDLER) {
                    DbgLog(L"WorkerThread'de yakalanmamis exception yutuldu.");
                }
                return 0;
            },
            hModule, 0, nullptr);

        if (hThread)
            CloseHandle(hThread);
        else
            DbgLog(L"KRITIK HATA: CreateThread basarisiz! (err=%lu)", GetLastError());
    }
    return TRUE;
}
