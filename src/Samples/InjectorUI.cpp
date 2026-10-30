// InjectorUI.cpp

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <thread>
#include <chrono>

// BLACKBONE FIX: MMap.h icinde Process& kullaniliyor ama forward decl yok
namespace blackbone { class Process; }

#include "BlackBone/Process/Process.h"
#include "BlackBone/ManualMap/MMap.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "Blackbone.lib")

using namespace blackbone;

bool WaitForModuleXign(Process& proc, const wchar_t* modName, DWORD timeoutMs);
bool SuspendProcessXign(Process& proc);
bool ResumeProcessXign(Process& proc);
bool RemoteUnlinkModule(Process& proc, uintptr_t modBase, const wchar_t* tag);
// ==================== UI IDs ====================
#define IDC_EDIT_KO_PATH    101
#define IDC_BTN_BROWSE_KO   102
#define IDC_EDIT_DLL_PATH   103
#define IDC_BTN_BROWSE_DLL  104
#define IDC_BTN_LAUNCH      105
#define IDC_BTN_INJECT      106
#define IDC_LOG             107

// ==================== GLOBALS ====================
HWND g_hEditKo = nullptr;
HWND g_hEditDll = nullptr;
HWND g_hBtnLaunch = nullptr;
HWND g_hBtnInject = nullptr;
HWND g_hLog = nullptr;
DWORD g_koPid = 0;

// ==================== LOGGING ====================
void Log(const wchar_t* fmt, ...)
{
    if (!g_hLog) return;
    wchar_t buf[2048] = {};
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t prefix[64];
    swprintf_s(prefix, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    std::wstring line = std::wstring(prefix) + buf + L"\r\n";

    int len = (int)SendMessageW(g_hLog, WM_GETTEXTLENGTH, 0, 0);
    SendMessageW(g_hLog, EM_SETSEL, len, len);
    SendMessageW(g_hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(g_hLog, EM_SCROLLCARET, 0, 0);
}

// ==================== FILE BROWSER ====================
std::wstring BrowseFile(HWND hOwner, const wchar_t* filter, const wchar_t* title)
{
    wchar_t fileName[MAX_PATH] = {};
    OPENFILENAMEW ofn = { sizeof(ofn) };
    ofn.hwndOwner = hOwner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) return fileName;
    return L"";
}

// ==================== PERFORM INJECT ====================
bool PerformInject(DWORD pid, const std::wstring& dllPath)
{
    Log(L"==============================");
    Log(L"[*] Process attach: PID=%lu", pid);

    Process proc;
    NTSTATUS st = proc.Attach(pid);
    if (!NT_SUCCESS(st))
    {
        Log(L"[-] HATA: Attach basarisiz. NTSTATUS=0x%08X", st);
        return false;
    }

    // ---- TIMING 1: x3.xem bekle (max 25sn) ----
    Log(L"[*] Timing: x3.xem bekleniyor (limit 25sn)...");
    if (!WaitForModuleXign(proc, L"x3.xem", 25000))
    {
        Log(L"[!] UYARI: x3.xem zaman asimi, devam ediliyor...");
    }
    else
    {
        Log(L"[+] x3.xem bellekte tespit edildi.");
    }

    // ---- TIMING 2: Sureci dondur ----
    Log(L"[*] Timing: NtSuspendProcess");
    if (!SuspendProcessXign(proc))
    {
        Log(L"[-] HATA: Suspend basarisiz.");
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    // ---- TIMING 3: x3.xem PEB unlink ----
    auto x3mod = proc.modules().GetModule(L"x3.xem");
    if (x3mod)
    {
        uintptr_t x3base = static_cast<uintptr_t>(x3mod->baseAddress);
        Log(L"[+] x3.xem Base: 0x%p", (void*)x3base);
        RemoteUnlinkModule(proc, x3base, L"x3.xem");
    }
    else
    {
        Log(L"[!] x3.xem base bulunamadi, unlink atlaniyor.");
    }
    // ---- TIMING 4: Manual Map inject ----
    Log(L"[*] Timing: Manual Map inject: %s", dllPath.c_str());

    auto image = proc.mmap().MapImage(
        dllPath.c_str(),
        eLoadFlags::WipeHeader | eLoadFlags::HideVAD
    );

    if (!image)
    {
        Log(L"[-] HATA: MapImage basarisiz. Status=0x%08X", image.status);
        ResumeProcessXign(proc);
        return false;
    }

    // call_result_t::result() fonksiyonu cagriliyor
    auto modInfo = image.result();
    uintptr_t imgBase = static_cast<uintptr_t>(modInfo->baseAddress);

    Log(L"[+] Manual Map OK. Base=0x%p", (void*)imgBase);


    // ---- TIMING 5: DLL PEB'den gizle ----
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    RemoteUnlinkModule(proc, imgBase, L"berat.dll");

    // ---- TIMING 6: Sureci devam ettir ----
    Log(L"[*] Timing: NtResumeProcess");
    ResumeProcessXign(proc);

    Log(L"[+] INJECT TAMAMLANDI.");
    proc.Detach();
    return true;
}

// ==================== WIN32 UI ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int x = 10, y = 10;

        CreateWindowExW(0, L"STATIC", L"KnightOnline.exe Path:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditKo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 360, 20, hWnd, (HMENU)IDC_EDIT_KO_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 520, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_KO, nullptr, nullptr);

        y += 35;

        CreateWindowExW(0, L"STATIC", L"Inject DLL:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditDll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"berat.dll",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 360, 20, hWnd, (HMENU)IDC_EDIT_DLL_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 520, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_DLL, nullptr, nullptr);

        y += 45;

        g_hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Oyunu Baslat",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 150, y, 120, 28, hWnd, (HMENU)IDC_BTN_LAUNCH, nullptr, nullptr);

        g_hBtnInject = CreateWindowExW(0, L"BUTTON", L"Inject Et",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, x + 290, y, 120, 28, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);

        y += 45;

        CreateWindowExW(0, L"STATIC", L"Log:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 50, 18, hWnd, nullptr, nullptr, nullptr);

        y += 20;
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            x, y, 600, 260, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);

        SendMessageW(g_hEditKo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hEditDll, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);

        Log(L"UI Yuklendi. Knight Online path ve DLL secip 'Oyunu Baslat' tikla.");
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_BTN_BROWSE_KO)
        {
            auto path = BrowseFile(hWnd, L"Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0\0", L"KnightOnline.exe Sec");
            if (!path.empty()) SetWindowTextW(g_hEditKo, path.c_str());
        }
        else if (LOWORD(wParam) == IDC_BTN_BROWSE_DLL)
        {
            auto path = BrowseFile(hWnd, L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0\0", L"DLL Sec");
            if (!path.empty()) SetWindowTextW(g_hEditDll, path.c_str());
        }
        else if (LOWORD(wParam) == IDC_BTN_LAUNCH)
        {
            wchar_t koPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditKo, koPath, MAX_PATH);
            if (!*koPath)
            {
                MessageBoxW(hWnd, L"Lutfen KnightOnline.exe yolunu secin.", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t workDir[MAX_PATH] = {};
            wcscpy_s(workDir, koPath);
            wchar_t* lastSlash = wcsrchr(workDir, L'\\');
            if (lastSlash) *lastSlash = 0;

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};
            wchar_t cmdLine[MAX_PATH * 2] = {};
            swprintf_s(cmdLine, L"\"%s\"", koPath);

            Log(L"[*] CreateProcess: %s", koPath);
            if (CreateProcessW(koPath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, workDir, &si, &pi))
            {
                g_koPid = pi.dwProcessId;
                CloseHandle(pi.hThread);

                Log(L"[+] Oyun baslatildi. PID: %lu", g_koPid);
                EnableWindow(g_hBtnInject, TRUE);
            }
            else
            {
                Log(L"[-] CreateProcess HATA: %lu", GetLastError());
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_INJECT)
        {
            if (g_koPid == 0)
            {
                MessageBoxW(hWnd, L"Once oyunu baslatin.", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t dllPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);
            if (!*dllPath)
            {
                MessageBoxW(hWnd, L"DLL yolu bos.", L"Uyari", MB_ICONWARNING);
                break;
            }

            EnableWindow(g_hBtnInject, FALSE);
            EnableWindow(g_hBtnLaunch, FALSE);

            std::thread([hWnd, path = std::wstring(dllPath)]() mutable {
                PerformInject(g_koPid, path);
                PostMessageW(hWnd, WM_APP + 1, 0, 0);
            }).detach();
        }
        break;
    }

    case WM_APP + 1:
        EnableWindow(g_hBtnInject, TRUE);
        EnableWindow(g_hBtnLaunch, TRUE);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ==================== ENTRY POINT ====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.lpszClassName = L"KOInjectorByBlackbone";

    if (!RegisterClassExW(&wc)) return 1;

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName,
        L"Knight Online XignCode Bypass Injector (Blackbone UI)",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 660, 480,
        nullptr, nullptr, hInstance, nullptr);

    if (!hWnd) return 1;

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
