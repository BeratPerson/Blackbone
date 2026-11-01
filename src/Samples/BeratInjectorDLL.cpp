// BeratInjectorDLL.cpp
// x3.xem sureci bulunca berat.dll'i manual map ile inject eder ve bypass yapar.
// Bu DLL herhangi bir loader tarafindan yuklendiginde DllMain icinden devreye girer.

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstdarg>

#include "BlackBone/Process/Process.h"
#include "BlackBone/ManualMap/MMap.h"
#include "BlackBone/DriverControl/DriverControl.h"
#include "ManualMapXign.h"
#include "XignProcessController.hpp"

#pragma comment(lib, "Blackbone.lib")

using namespace blackbone;

// ====================================================
// Konfigürasyon
// ====================================================
static const wchar_t* TARGET_PROCESS  = L"x3.xem";
static const wchar_t* XIGN_MODULE     = L"x3.xem";   // XignCode modul adi
static const wchar_t* INJECT_DLL_NAME = L"berat.dll"; // Inject edilecek DLL

static const DWORD POLL_INTERVAL_MS   = 500;   // Surec tarama araligi
static const DWORD MAX_WAIT_SEC       = 180;   // Maksimum bekleme (saniye)
static const DWORD XIGN_WAIT_MS       = 6000;  // x3.xem yuklenme bekleme suresi

// ====================================================
// Dahili Log (OutputDebugString'e yazilir)
// ====================================================
static void DbgLog(const wchar_t* fmt, ...)
{
    wchar_t buf[2048] = {};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    OutputDebugStringW(buf);
    OutputDebugStringW(L"\n");
}

// ====================================================
// berat.dll'in tam yolunu injector DLL'in yanina bakarak bulur
// ====================================================
static std::wstring ResolveDllPath(HMODULE hSelf)
{
    wchar_t selfPath[MAX_PATH] = {};
    GetModuleFileNameW(hSelf, selfPath, MAX_PATH);

    // Son backslash'a kadar kes -> dizin
    wchar_t* lastSlash = wcsrchr(selfPath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';

    std::wstring result = selfPath;
    result += INJECT_DLL_NAME;
    return result;
}

// ====================================================
// PEB SelfUnlink (bu DLL kendi kendini gizler)
// ====================================================
typedef struct _LDR_FULL {
    ULONG Length;
    BOOLEAN Initialized;
    PVOID SsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
} LDR_FULL, *PLDR_FULL;

typedef struct _LDR_ENTRY_FULL {
    LIST_ENTRY InLoadOrderLinks;
    LIST_ENTRY InMemoryOrderLinks;
    LIST_ENTRY InInitializationOrderLinks;
    PVOID      DllBase;
    PVOID      EntryPoint;
    ULONG      SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
} LDR_ENTRY_FULL, *PLDR_ENTRY_FULL;

static void SelfUnlinkPEB(HMODULE hSelf)
{
    if (!hSelf) return;

#ifdef _WIN64
    PPEB peb = (PPEB)__readgsqword(0x60);
#else
    PPEB peb = (PPEB)__readfsdword(0x30);
#endif
    if (!peb || !peb->Ldr) return;

    PLDR_FULL ldr = (PLDR_FULL)peb->Ldr;
    PLIST_ENTRY head = &ldr->InLoadOrderModuleList;
    PLIST_ENTRY entry = head->Flink;

    while (entry != head)
    {
        PLDR_ENTRY_FULL mod = CONTAINING_RECORD(entry, LDR_ENTRY_FULL, InLoadOrderLinks);
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
            DbgLog(L"[BeratDLL] PEB unlink tamamlandi.");
            break;
        }
    }
}

// ====================================================
// Ana Enjeksiyon Worker Thread
// ====================================================
struct WorkerParams
{
    HMODULE hSelf;
};

static DWORD WINAPI InjectorWorker(LPVOID param)
{
    WorkerParams* wp = static_cast<WorkerParams*>(param);
    HMODULE hSelf = wp ? wp->hSelf : nullptr;
    delete wp;

    // XignCode'un init dongusundan once yerles
    Sleep(2000);

    // Kendi kendini PEB'den gizle
    __try { SelfUnlinkPEB(hSelf); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}

    // Inject edilecek DLL yolu: injector DLL'in yanindaki berat.dll
    std::wstring dllPath = ResolveDllPath(hSelf);
    DbgLog(L"[BeratDLL] Hedef DLL yolu: %s", dllPath.c_str());

    // Dosya varligini dogrula
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        DbgLog(L"[BeratDLL] HATA: %s bulunamadi! Injector durduruluyor.", dllPath.c_str());
        return 1;
    }

    DbgLog(L"[BeratDLL] '%s' sureci bekleniyor (maks. %lu sn)...", TARGET_PROCESS, MAX_WAIT_SEC);

    // ---- Surec Tarama Dongusu ----
    DWORD targetPid = 0;
    DWORD elapsed = 0;

    while (elapsed < MAX_WAIT_SEC * 1000)
    {
        targetPid = FindProcessByName(TARGET_PROCESS);
        if (targetPid != 0)
        {
            DbgLog(L"[BeratDLL] '%s' bulundu! PID: %lu", TARGET_PROCESS, targetPid);
            break;
        }
        Sleep(POLL_INTERVAL_MS);
        elapsed += POLL_INTERVAL_MS;
    }

    if (targetPid == 0)
    {
        DbgLog(L"[BeratDLL] Zaman asimi: '%s' %lu sn icinde gorulmedi.", TARGET_PROCESS, MAX_WAIT_SEC);
        return 1;
    }

    // ---- Blackbone Injection ----
    XignSamples::XignProcessController controller;
    controller.SetLogCallback([](const wchar_t* msg) {
        DbgLog(L"%s", msg);
    });

    DbgLog(L"[BeratDLL] Enjeksiyon baslatiyor...");
    auto result = controller.ExecuteInjection(targetPid, dllPath);

    if (result.Success)
    {
        DbgLog(L"[BeratDLL] ===========================================");
        DbgLog(L"[BeratDLL] ENJEKSIYON BASARIYLA TAMAMLANDI!");
        DbgLog(L"[BeratDLL] MappedBase: 0x%p", (void*)result.MappedBase);
        DbgLog(L"[BeratDLL] ===========================================");
    }
    else
    {
        DbgLog(L"[BeratDLL] ENJEKSIYON BASARISIZ! Mesaj: %s", result.StatusMessage.c_str());
        DbgLog(L"[BeratDLL] Basarisiz Adim: %d | Win32Err: %lu | NtStatus: 0x%08X",
            (int)result.FailedStep, result.LastWin32Error, (uint32_t)result.LastNtStatus);
    }

    return result.Success ? 0 : 2;
}

// ====================================================
// DllMain
// ====================================================
extern "C" BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        WorkerParams* params = new WorkerParams{ hModule };
        HANDLE hThread = CreateThread(nullptr, 0, InjectorWorker, params, 0, nullptr);
        if (hThread)
            CloseHandle(hThread);
        else
            delete params;
    }
    return TRUE;
}
