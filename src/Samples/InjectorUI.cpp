// InjectorUI.cpp
// Blackbone tabanli XignCode3 Bypass Injector — Samples.exe
// Hedef: x3.xem sureci | Payload: berat.dll
// Ozellikler: Manual Map + PEB Unlink + Heartbeat Bypass Zamanlama

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <thread>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>

#include "BlackBone/Process/Process.h"
#include "BlackBone/ManualMap/MMap.h"
#include "BlackBone/DriverControl/DriverControl.h"
#include "ManualMapXign.h"
#include "XignProcessController.hpp"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "Blackbone.lib")
#pragma comment(lib, "Advapi32.lib")

using namespace blackbone;

// ====================================================================
// UI ID'leri
// ====================================================================
#define IDC_EDIT_PROC_NAME      101
#define IDC_BTN_REFRESH_PROC    102
#define IDC_EDIT_DLL_PATH       103
#define IDC_BTN_BROWSE_DLL      104
#define IDC_EDIT_STATUS         105
#define IDC_CHK_AUTO_INJECT     106
#define IDC_BTN_ATTACH          107
#define IDC_BTN_INJECT          108
#define IDC_BTN_CLEAR_LOG       109
#define IDC_LOG                 110
#define IDC_BTN_LAUNCH          111
#define IDC_EDIT_KO_PATH        112
#define IDC_BTN_BROWSE_KO       113
#define IDC_EDIT_ARGS           114
#define IDC_STATUS_BAR          115

// ====================================================================
// Globals
// ====================================================================
static HWND g_hMainWnd         = nullptr;  // Ana pencere — PostMessage hedefi
static HWND g_hEditProcName    = nullptr;
static HWND g_hEditDll         = nullptr;
static HWND g_hEditKo          = nullptr;
static HWND g_hEditArgs        = nullptr;
static HWND g_hChkAutoInject   = nullptr;
static HWND g_hBtnAttach       = nullptr;
static HWND g_hBtnInject       = nullptr;
static HWND g_hLog             = nullptr;
static HWND g_hStatusBar       = nullptr;
static DWORD g_targetPid       = 0;
static XignSamples::XignProcessController g_controller;

// ====================================================================
// Loglama — zaman damgali, renkli prefix
// ====================================================================
static void Log(const wchar_t* fmt, ...)
{
    if (!g_hLog) return;

    wchar_t msg[4096] = {};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(msg, _countof(msg), _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[4200] = {};
    _snwprintf_s(line, _countof(line), _TRUNCATE,
        L"[%02d:%02d:%02d.%03d] %s\r\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    // Thread-safe UI guncelleme — mesaji ANA pencereye gonder, g_hLog'a degil.
    // g_hLog bir EDIT kontroldür; kendi WndProc'u WM_APP+1'i ignore eder.
    if (!g_hMainWnd) return;
    struct Ctx { HWND h; std::wstring s; };
    auto* ctx = new Ctx{ g_hLog, line };
    PostMessageW(g_hMainWnd, WM_APP + 1, 0, reinterpret_cast<LPARAM>(ctx));
}

// ====================================================================
// Durum cubugu guncelle
// ====================================================================
static void SetStatus(const wchar_t* text)
{
    if (g_hStatusBar)
        SetWindowTextW(g_hStatusBar, text);
}

// ====================================================================
// Dosya secici
// ====================================================================
static std::wstring BrowseFile(HWND hOwner, const wchar_t* filter, const wchar_t* title)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner   = hOwner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = fileName;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = title;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return fileName;
    return L"";
}

// ====================================================================
// Sistem bilgisi loglama
// ====================================================================
static void LogSystemInfo()
{
    Log(L"====================================================================");
    Log(L"  berat Injector v2.0  |  Blackbone Manual Map  |  XignCode3 Bypass");
    Log(L"====================================================================");

    RTL_OSVERSIONINFOEXW osi = {};
    osi.dwOSVersionInfoSize = sizeof(osi);
    using RtlGetVersionFn = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto RtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (RtlGetVersion && RtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&osi)) == 0)
    {
        Log(L"[SYS] Windows %lu.%lu Build %lu (SP %d.%d) %s",
            osi.dwMajorVersion, osi.dwMinorVersion, osi.dwBuildNumber,
            osi.wServicePackMajor, osi.wServicePackMinor,
            osi.wProductType == VER_NT_WORKSTATION ? L"Workstation" : L"Server");
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    Log(L"[SYS] CPU: %lu core | Arch: %s | PageSize: 0x%lX",
        si.dwNumberOfProcessors,
        si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? L"x64" :
        si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? L"x86" : L"?",
        si.dwPageSize);

    DWORD myPid = GetCurrentProcessId();
    Log(L"[SYS] Injector PID: %lu", myPid);
    Log(L"====================================================================");
}

// ====================================================================
// Process listesini güncelle
// ====================================================================
static void RefreshProcessStatus()
{
    wchar_t procName[MAX_PATH] = {};
    GetWindowTextW(g_hEditProcName, procName, MAX_PATH);

    if (!*procName)
    {
        SetStatus(L"Surec adi bos.");
        return;
    }

    DWORD pid = FindProcessByName(procName);
    if (pid != 0)
    {
        g_targetPid = pid;
        wchar_t statusBuf[128] = {};
        _snwprintf_s(statusBuf, _countof(statusBuf), _TRUNCATE,
            L"  [HAZIR]  %s  PID: %lu  — Inject edilebilir", procName, pid);
        SetStatus(statusBuf);
        EnableWindow(g_hBtnInject, TRUE);
        Log(L"[+] Surec bulundu: '%s' PID=%lu", procName, pid);
    }
    else
    {
        g_targetPid = 0;
        wchar_t statusBuf[128] = {};
        _snwprintf_s(statusBuf, _countof(statusBuf), _TRUNCATE,
            L"  [BEKLENIYOR]  '%s' henuz calismıyor...", procName);
        SetStatus(statusBuf);
        EnableWindow(g_hBtnInject, FALSE);
        Log(L"[-] Surec bulunamadi: '%s'", procName);
    }
}

// ====================================================================
// Injection — asenkron
// ====================================================================
static void PerformInjectAsync(DWORD pid, const std::wstring& dllPath, const std::wstring& procName)
{
    EnableWindow(g_hBtnInject, FALSE);
    EnableWindow(g_hBtnAttach, FALSE);

    std::thread([pid, dllPath, procName]()
    {
        Log(L"====================================================================");
        Log(L"[*] ENJEKSIYON BASLADI");
        Log(L"    Hedef Surec : %s (PID: %lu)", procName.c_str(), pid);
        Log(L"    Payload DLL : %s", dllPath.c_str());
        Log(L"====================================================================");

        SetStatus(L"  [CALISIYOR]  Enjeksiyon devam ediyor...");

        g_controller.SetLogCallback([](const wchar_t* msg) {
            Log(L"%s", msg);
        });

        // Dosya boyutu
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(dllPath.c_str(), GetFileExInfoStandard, &fad))
        {
            ULARGE_INTEGER sz;
            sz.LowPart  = fad.nFileSizeLow;
            sz.HighPart = fad.nFileSizeHigh;
            Log(L"[*] DLL boyutu: %llu byte (%.1f KB)", sz.QuadPart, sz.QuadPart / 1024.0);
        }

        // Inject et
        auto t0 = std::chrono::high_resolution_clock::now();
        auto result = g_controller.ExecuteInjection(pid, dllPath);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0).count();

        Log(L"====================================================================");
        if (result.Success)
        {
            Log(L"[OK] ENJEKSIYON BASARILI  (%lld ms)", elapsed);
            Log(L"[OK] MappedBase : 0x%p", (void*)result.MappedBase);
            Log(L"[OK] Payload    : %s", dllPath.c_str());
            Log(L"[OK] Hedef      : %s PID=%lu", procName.c_str(), pid);
            Log(L"[OK] Durum      : HeartbeatBypass payload icinde kurulacak");
            SetStatus(L"  [BASARILI]  Enjeksiyon tamamlandi!");
        }
        else
        {
            Log(L"[HATA] ENJEKSIYON BASARISIZ  (%lld ms)", elapsed);
            Log(L"[HATA] Adim       : %d", static_cast<int>(result.FailedStep));
            Log(L"[HATA] Mesaj      : %s", result.StatusMessage.c_str());
            Log(L"[HATA] Win32 Hata : %lu (0x%08lX)", result.LastWin32Error, result.LastWin32Error);
            Log(L"[HATA] NTSTATUS   : 0x%08X", static_cast<uint32_t>(result.LastNtStatus));

            // Adim aciklamasi
            const wchar_t* stepNames[] = {
                L"Idle", L"ValidatingInputs", L"AttachingProcess",
                L"WaitingMainModule", L"WaitingAntiCheatModule",
                L"SuspendingProcess", L"UnlinkingPeb", L"ResumingProcess",
                L"DriverRegistration", L"MappingImage",
                L"PostMappingCleanup", L"DetachingProcess",
                L"Completed", L"Failed"
            };
            int si = static_cast<int>(result.FailedStep);
            if (si >= 0 && si < 14)
                Log(L"[HATA] Hata Adimi : %s", stepNames[si]);

            SetStatus(L"  [HATA]  Enjeksiyon basarisiz oldu!");
        }
        Log(L"====================================================================");

        EnableWindow(g_hBtnInject, TRUE);
        EnableWindow(g_hBtnAttach, TRUE);

    }).detach();
}

// ====================================================================
// xhunter servis / device durumu kontrolu
// Raporda her iki driver da xhunter1.sys ama servis adi xhunter1 veya
// xhunter2 olabilir. Her ikisini + device path'i dene.
// ====================================================================
static bool IsXhunter2Running()
{
    // --- Servis kontrol ---
    {
        const wchar_t* svcNames[] = { L"xhunter2", L"xhunter1" };
        SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (scm)
        {
            for (auto name : svcNames)
            {
                SC_HANDLE svc = OpenServiceW(scm, name, SERVICE_QUERY_STATUS);
                if (svc)
                {
                    SERVICE_STATUS ss = {};
                    bool running = QueryServiceStatus(svc, &ss) &&
                                   (ss.dwCurrentState == SERVICE_RUNNING);
                    CloseServiceHandle(svc);
                    if (running) { CloseServiceHandle(scm); return true; }
                }
            }
            CloseServiceHandle(scm);
        }
    }
    // --- Device path fallback (servis adi bilinemiyorsa) ---
    {
        const wchar_t* devPaths[] = { L"\\\\.\\xhunter2", L"\\\\.\\xhunter1" };
        for (auto path : devPaths)
        {
            HANDLE h = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return true; }
        }
    }
    return false;
}

// ====================================================================
// Launch Watcher — bilinenPID icin xhunter2 bekle, sonra inject et
// "Oyunu Baslat" akisi: process zaten var, isimle arama yapma
// ====================================================================
static void StartLaunchWatcher(DWORD pid, const std::wstring& dllPath, const std::wstring& procName)
{
    std::thread([pid, dllPath, procName]()
    {
        Log(L"[*] Launch watcher: PID=%lu — xhunter servisi bekleniyor (maks. 90 sn)...", pid);
        SetStatus(L"  [BEKLENIYOR]  xhunter yukleniyor...");

        bool xh2Ready = false;
        for (int w = 0; w < 180; ++w)
        {
            if (IsXhunter2Running()) { xh2Ready = true; break; }
            if (w % 6 == 0)
                Log(L"[...] xhunter bekleniyor... (%d sn gecti)", w / 2);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        if (xh2Ready)
            Log(L"[+] xhunter servis AKTIF. Stabilizasyon icin 3 sn...");
        else
            Log(L"[!] xhunter 90 sn icinde bulunamadi — devam ediliyor.");

        std::this_thread::sleep_for(std::chrono::seconds(3));

        // Hedef process hala calisiyor mu?
        HANDLE hChk = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!hChk)
        {
            Log(L"[!] PID=%lu kapandi, inject iptal.", pid);
            return;
        }
        DWORD exitCode = STILL_ACTIVE;
        GetExitCodeProcess(hChk, &exitCode);
        CloseHandle(hChk);
        if (exitCode != STILL_ACTIVE)
        {
            Log(L"[!] PID=%lu sonlandi (ExitCode=%lu), inject iptal.", pid, exitCode);
            return;
        }

        Log(L"[*] Enjeksiyon baslatiliyor -> PID=%lu '%s'", pid, procName.c_str());
        PerformInjectAsync(pid, dllPath, procName);
    }).detach();
}

// ====================================================================
// Watcher Thread — surec ADINA gore tara, bul, inject et
// "Surece Baglan" akisi (manuel/zaten calisan oyun)
// ====================================================================
static void StartWatcher(const std::wstring& procName, const std::wstring& dllPath)
{
    std::thread([procName, dllPath]()
    {
        Log(L"[*] Watcher devreye girdi. '%s' bekleniyor (maks. 120 sn)...", procName.c_str());
        SetStatus(L"  [IZLENIYOR]  Surec bekleniyor...");

        for (int i = 0; i < 240; ++i)
        {
            DWORD pid = FindProcessByName(procName.c_str());
            if (pid != 0)
            {
                g_targetPid = pid;
                Log(L"[+] '%s' algilandi! PID: %lu", procName.c_str(), pid);
                EnableWindow(g_hBtnInject, TRUE);

                // xhunter2 kernel driver tamamen yuklenmeden enjekte etme
                Log(L"[*] xhunter2 servisi bekleniyor (maks. 60 sn)...");
                bool xh2Ready = false;
                for (int w = 0; w < 120; ++w)
                {
                    if (IsXhunter2Running())
                    {
                        xh2Ready = true;
                        break;
                    }
                    if (w % 6 == 0)
                        Log(L"[...] xhunter2 bekleniyor... (%d sn gecti)", w / 2);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }

                if (xh2Ready)
                    Log(L"[+] xhunter2 servis AKTIF. Stabilizasyon icin 3 sn bekleniyor...");
                else
                    Log(L"[!] xhunter2 servis 60 sn icinde aktif olmadi — yine de devam ediliyor.");

                std::this_thread::sleep_for(std::chrono::seconds(3));

                LRESULT chk = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (chk == BST_CHECKED)
                {
                    Log(L"[*] Otomatik inject devrede — baslatiliyor...");
                    PerformInjectAsync(pid, dllPath, procName);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        Log(L"[!] Zaman asimi: '%s' 120 sn icinde baslamadi.", procName.c_str());
        SetStatus(L"  [ZAMAN ASIMI]  Surec bulunamadi.");

    }).detach();
}

// ====================================================================
// Oyun Launcher yolundan calisma dizinini al
// ====================================================================
static std::wstring GetWorkDir(const wchar_t* path)
{
    wchar_t dir[MAX_PATH] = {};
    wcscpy_s(dir, path);
    wchar_t* last = wcsrchr(dir, L'\\');
    if (last) *last = 0;
    else GetCurrentDirectoryW(MAX_PATH, dir);
    return dir;
}

// ====================================================================
// WndProc
// ====================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_APP + 1:
    {
        // Thread-safe log yazma
        struct Ctx { HWND h; std::wstring s; };
        auto* ctx = reinterpret_cast<Ctx*>(lParam);
        if (ctx && ctx->h)
        {
            int len = (int)SendMessageW(ctx->h, WM_GETTEXTLENGTH, 0, 0);
            SendMessageW(ctx->h, EM_SETSEL, len, len);
            SendMessageW(ctx->h, EM_REPLACESEL, FALSE, (LPARAM)ctx->s.c_str());
            SendMessageW(ctx->h, EM_SCROLLCARET, 0, 0);
        }
        delete ctx;
        return 0;
    }

    case WM_CREATE:
    {
        HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        if (!hFont) hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        int x = 12, y = 10;

        // --- Hedef Surec ---
        CreateWindowExW(0, L"STATIC", L"Hedef Surec:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y + 2, 110, 18, hWnd, nullptr, nullptr, nullptr);
        g_hEditProcName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"x3.xem",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x + 115, y, 230, 22, hWnd, (HMENU)IDC_EDIT_PROC_NAME, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Tara",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + 355, y - 1, 70, 24, hWnd, (HMENU)IDC_BTN_REFRESH_PROC, nullptr, nullptr);

        y += 32;

        // --- Payload DLL ---
        CreateWindowExW(0, L"STATIC", L"Payload DLL:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y + 2, 110, 18, hWnd, nullptr, nullptr, nullptr);
        // Varsayilan DLL yolu — exe'nin bulundugu dizin + berat.dll
        wchar_t defaultDllPath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, defaultDllPath, MAX_PATH);
        { wchar_t* bs = wcsrchr(defaultDllPath, L'\\'); if (bs) { *(bs+1)=0; wcscat_s(defaultDllPath, L"berat.dll"); } }

        g_hEditDll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultDllPath,
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x + 115, y, 230, 22, hWnd, (HMENU)IDC_EDIT_DLL_PATH, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + 355, y - 1, 70, 24, hWnd, (HMENU)IDC_BTN_BROWSE_DLL, nullptr, nullptr);

        y += 32;

        // --- Oyun EXE (opsiyonel launch) ---
        CreateWindowExW(0, L"STATIC", L"Oyun EXE (opsiyonel):",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y + 2, 145, 18, hWnd, nullptr, nullptr, nullptr);
        g_hEditKo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
            x + 150, y, 195, 22, hWnd, (HMENU)IDC_EDIT_KO_PATH, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + 355, y - 1, 70, 24, hWnd, (HMENU)IDC_BTN_BROWSE_KO, nullptr, nullptr);

        y += 32;

        // --- Launch Args ---
        CreateWindowExW(0, L"STATIC", L"Baslatma Args:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y + 2, 110, 18, hWnd, nullptr, nullptr, nullptr);
        {
            wchar_t pidStr[32] = {};
            _snwprintf_s(pidStr, _countof(pidStr), _TRUNCATE, L"%lu", GetCurrentProcessId());
            g_hEditArgs = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pidStr,
                WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL,
                x + 115, y, 100, 22, hWnd, (HMENU)IDC_EDIT_ARGS, nullptr, nullptr);
        }
        CreateWindowExW(0, L"STATIC",
            L"← procID argumani  (!-(procID) kontrolunu asar)",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            x + 225, y + 2, 320, 18, hWnd, nullptr, nullptr, nullptr);

        y += 32;

        // --- Checkbox ---
        g_hChkAutoInject = CreateWindowExW(0, L"BUTTON",
            L"Otomatik Inject Et (Surec algilandigi anda)",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
            x + 115, y, 360, 22, hWnd, (HMENU)IDC_CHK_AUTO_INJECT, nullptr, nullptr);
        SendMessageW(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

        y += 34;

        // --- Butonlar ---
        CreateWindowExW(0, L"BUTTON", L"Oyunu Baslat",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x, y, 110, 30, hWnd, (HMENU)IDC_BTN_LAUNCH, nullptr, nullptr);

        g_hBtnAttach = CreateWindowExW(0, L"BUTTON", L"Surece Baglan",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + 120, y, 120, 30, hWnd, (HMENU)IDC_BTN_ATTACH, nullptr, nullptr);

        g_hBtnInject = CreateWindowExW(0, L"BUTTON", L"Inject Et",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED,
            x + 250, y, 100, 30, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Logu Temizle",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            x + 360, y, 100, 30, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, nullptr, nullptr);

        y += 42;

        // --- Durum cubugu ---
        g_hStatusBar = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC",
            L"  Hazir — Surec adi girin ve 'Tara' butonuna basin.",
            WS_VISIBLE | WS_CHILD | SS_LEFT,
            x, y, 650, 22, hWnd, (HMENU)IDC_STATUS_BAR, nullptr, nullptr);

        y += 30;

        // --- Log alanı ---
        CreateWindowExW(0, L"STATIC", L"Islem Loglari:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 120, 18, hWnd, nullptr, nullptr, nullptr);
        y += 20;

        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL | WS_HSCROLL,
            x, y, 650, 290, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);

        // Font ata
        auto setFont = [&](HWND h) {
            if (h) SendMessageW(h, WM_SETFONT, (WPARAM)hFont, TRUE);
        };
        setFont(g_hEditProcName); setFont(g_hEditDll); setFont(g_hEditKo);
        setFont(g_hEditArgs); setFont(g_hChkAutoInject);
        setFont(g_hLog); setFont(g_hStatusBar);

        SetXignLogCallback(Log);

        // Baslatma logosu
        LogSystemInfo();
        Log(L"[*] Hazir. Hedef: x3.xem | Payload: berat.dll");
        Log(L"[*] 'Tara' ile süreci algila veya 'Surece Baglan' ile mevcut oyuna baglan.");
        Log(L"[*] Inject edilecek DLL, oyun icinde:");
        Log(L"    1. PEB'den kendi girisini kaldirir (gizlenir)");
        Log(L"    2. x3.xem validation handler'larini patch eder");
        Log(L"    3. Heartbeat / integrity check'leri bypass eder");

        return 0;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);

        if (id == IDC_BTN_BROWSE_DLL)
        {
            auto p = BrowseFile(hWnd, L"DLL (*.dll)\0*.dll\0Tumü (*.*)\0*.*\0\0", L"Payload DLL Sec");
            if (!p.empty()) {
                SetWindowTextW(g_hEditDll, p.c_str());
                Log(L"[>] DLL secildi: %s", p.c_str());
            }
        }
        else if (id == IDC_BTN_BROWSE_KO)
        {
            auto p = BrowseFile(hWnd, L"Executable (*.exe)\0*.exe\0Tumu (*.*)\0*.*\0\0", L"Oyun EXE Sec");
            if (!p.empty()) {
                SetWindowTextW(g_hEditKo, p.c_str());
                Log(L"[>] Oyun EXE secildi: %s", p.c_str());
            }
        }
        else if (id == IDC_BTN_REFRESH_PROC)
        {
            RefreshProcessStatus();
        }
        else if (id == IDC_BTN_CLEAR_LOG)
        {
            SetWindowTextW(g_hLog, L"");
        }
        else if (id == IDC_BTN_ATTACH)
        {
            wchar_t procName[MAX_PATH] = {};
            GetWindowTextW(g_hEditProcName, procName, MAX_PATH);
            if (!*procName) {
                MessageBoxW(hWnd, L"Hedef surec adi bos!", L"Uyari", MB_ICONWARNING);
                break;
            }

            Log(L"[*] Surec aranıyor: '%s'...", procName);
            DWORD pid = FindProcessByName(procName);

            if (pid == 0)
            {
                // x3.xem bulunamadiysa fallback aramalar
                wchar_t alt1[MAX_PATH] = {}, alt2[MAX_PATH] = {};
                _snwprintf_s(alt1, _countof(alt1), _TRUNCATE, L"%s.exe", procName);
                pid = FindProcessByName(alt1);
                if (!pid) {
                    _snwprintf_s(alt2, _countof(alt2), _TRUNCATE, L"KnightOnLine.exe");
                    pid = FindProcessByName(alt2);
                    if (pid) Log(L"[*] Fallback: KnightOnLine.exe bulundu PID=%lu", pid);
                }
            }

            if (pid != 0)
            {
                g_targetPid = pid;
                Log(L"[+] Baglanildi: '%s' PID=%lu", procName, pid);
                EnableWindow(g_hBtnInject, TRUE);

                wchar_t dllPath[MAX_PATH] = {};
                GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);

                LRESULT chk = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (chk == BST_CHECKED && *dllPath)
                {
                    Log(L"[*] Otomatik inject devrede...");
                    PerformInjectAsync(pid, dllPath, procName);
                }
            }
            else
            {
                Log(L"[-] Surec bulunamadi: '%s'. Oyunu once baslatın.", procName);
                SetStatus(L"  [BULUNAMADI]  Surec calismıyor.");
                MessageBoxW(hWnd,
                    L"Hedef surec calismıyor!\n\nOnce oyunu baslatın, sonra 'Surece Baglan'ı kullanin.",
                    L"Bulunamadi", MB_ICONINFORMATION);
            }
        }
        else if (id == IDC_BTN_INJECT)
        {
            if (g_targetPid == 0)
            {
                MessageBoxW(hWnd, L"Once surece baglanin!", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t dllPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);
            if (!*dllPath)
            {
                MessageBoxW(hWnd, L"Payload DLL yolu bos!", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t procName[MAX_PATH] = {};
            GetWindowTextW(g_hEditProcName, procName, MAX_PATH);
            PerformInjectAsync(g_targetPid, dllPath, procName);
        }
        else if (id == IDC_BTN_LAUNCH)
        {
            wchar_t koPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditKo, koPath, MAX_PATH);
            if (!*koPath)
            {
                MessageBoxW(hWnd, L"Oyun EXE yolu bos! Sec butonuna basin.", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t args[MAX_PATH] = {};
            GetWindowTextW(g_hEditArgs, args, MAX_PATH);
            if (!*args)
            {
                _snwprintf_s(args, _countof(args), _TRUNCATE, L"%lu", GetCurrentProcessId());
                SetWindowTextW(g_hEditArgs, args);
            }

            std::wstring workDir = GetWorkDir(koPath);

            wchar_t cmdLine[MAX_PATH * 2] = {};
            _snwprintf_s(cmdLine, _countof(cmdLine), _TRUNCATE, L"\"%s\" %s", koPath, args);

            Log(L"====================================================================");
            Log(L"[*] Oyun baslatiliyor...");
            Log(L"    Yol     : %s", koPath);
            Log(L"    Args    : %s", args);
            Log(L"    WorkDir : %s", workDir.c_str());

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};

            if (CreateProcessW(koPath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi))
            {
                g_targetPid = pi.dwProcessId;
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);

                Log(L"[+] Oyun baslatildi! PID: %lu", g_targetPid);
                EnableWindow(g_hBtnInject, TRUE);

                // Watcher ile x3.xem'i de izle (launcher->oyun akisi)
                wchar_t procName[MAX_PATH] = {};
                GetWindowTextW(g_hEditProcName, procName, MAX_PATH);
                wchar_t dllPath[MAX_PATH] = {};
                GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);

                LRESULT chk = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (chk == BST_CHECKED && *dllPath)
                {
                    // Bilinen PID'e inject: isimle arama yok, xhunter hazir olana kadar bekle
                    Log(L"[*] Launch watcher devreye girdi (PID=%lu). xhunter beklenecek...", g_targetPid);
                    StartLaunchWatcher(g_targetPid, dllPath, procName);
                }
            }
            else
            {
                Log(L"[-] CreateProcess HATA: %lu (0x%08lX)", GetLastError(), GetLastError());
                MessageBoxW(hWnd, L"Oyun baslatılamadi!\nDosya yolunu kontrol edin.", L"Hata", MB_ICONERROR);
            }
        }

        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ====================================================================
// WinMain
// ====================================================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    const wchar_t CLASS_NAME[] = L"BeratInjectorWnd_v2";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0, CLASS_NAME,
        L"berat",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 690, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 0;

    g_hMainWnd = hWnd;  // PostMessage hedefini kaydet

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
