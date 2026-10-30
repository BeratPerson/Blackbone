// InjectorUI.cpp
// Blackbone tabanli Knight Online XignCode3 Bypass Injector
// Detayli Loglama + procID & nullparam Cozumu + Launcher Oto-Yakala + Timing Manual Map

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <string>
#include <thread>
#include <chrono>

#include "BlackBone/Process/Process.h"
#include "BlackBone/ManualMap/MMap.h"
#include "ManualMapXign.h"

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "Blackbone.lib")

using namespace blackbone;

// ==================== UI IDs ====================
#define IDC_EDIT_KO_PATH        101
#define IDC_BTN_BROWSE_KO       102
#define IDC_EDIT_DLL_PATH       103
#define IDC_BTN_BROWSE_DLL      104
#define IDC_EDIT_ARGS           105
#define IDC_CHK_AUTO_INJECT     106
#define IDC_BTN_LAUNCH          107
#define IDC_BTN_LAUNCH_LAUNCHER 108
#define IDC_BTN_ATTACH          109
#define IDC_BTN_INJECT          110
#define IDC_BTN_CLEAR_LOG       111
#define IDC_LOG                 112

// ==================== GLOBALS ====================
HWND g_hEditKo = nullptr;
HWND g_hEditDll = nullptr;
HWND g_hEditArgs = nullptr;
HWND g_hChkAutoInject = nullptr;
HWND g_hBtnLaunch = nullptr;
HWND g_hBtnLaunchLauncher = nullptr;
HWND g_hBtnAttach = nullptr;
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

    SYSTEMTIME st; 
    GetLocalTime(&st);
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

// ==================== ASYNC INJECT ENGINE ====================
void PerformInjectAsync(DWORD pid, const std::wstring& dllPath)
{
    if (g_hBtnInject) EnableWindow(g_hBtnInject, FALSE);

    std::thread([pid, dllPath]() {
        // ==================== 0xC0000034 FIX ====================
        std::wstring absDllPath = dllPath;
        wchar_t buf[MAX_PATH] = {};
        DWORD len = GetFullPathNameW(dllPath.c_str(), MAX_PATH, buf, nullptr);
        if (len && len < MAX_PATH)
            absDllPath = buf;
        // =======================================================

        Log(L"==================================================");
        Log(L"[*] ENJEKSIYON ZAMANLAMASI BASLATILDI (PID: %lu)", pid);
        Log(L"[*] Hedef DLL Dosyasi: %s", absDllPath.c_str());

        // DLL dosyasini kontrol et (mutlak yolla)
        DWORD attr = GetFileAttributesW(absDllPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            Log(L"[-] HATA: DLL dosyasi bulunamadi! Yol: %s", absDllPath.c_str());
            if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
            return;
        }

        HANDLE hFile = CreateFileW(absDllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        // ... (kalan kod ayni) ...

        // ---- TIMING 4: Manual Map inject ----
        Log(L"[*] TIMING 4: Manual Map enjeksiyonu baslatiliyor...");
        Log(L"    -> Bayraklar: WipeHeader (PE Header Sil) | HideVAD (Bellek Sayfasini Gizle)");

        auto image = proc.mmap().MapImage(
            absDllPath.c_str(),          // <--- MUTLAK YOL
            eLoadFlags::WipeHeader | eLoadFlags::HideVAD
        );

        if (!image)
        {
            Log(L"[-] HATA: MapImage basarisiz! NTSTATUS: 0x%08X", image.status);
            Log(L"[*] Surec devam ettiriliyor (NtResumeProcess)...");
            ResumeProcessXign(proc);
            if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
            return;
        }

        auto modInfo = image.result();
        uintptr_t imgBase = static_cast<uintptr_t>(modInfo->baseAddress);
        Log(L"[+] Manual Map BASARILI!");
        // ... (geri kalan log ve PEB unlink ayni) ...
        }).detach();
}

// ==================== PROCESS WATCHER ====================
void StartProcessWatcher(const std::wstring& dllPath)
{
    std::thread([dllPath]() {
        Log(L"[*] Surec izleyici (Watcher) devrede. 'KnightOnLine.exe' bekleniyor...");
        Log(L"[*] (Eger Launcher actiysaniz 'Start' butonuna basiniz)");

        DWORD targetPid = 0;
        for (int i = 0; i < 120; ++i)
        {
            targetPid = FindProcessByName(L"KnightOnLine.exe");
            if (!targetPid) targetPid = FindProcessByName(L"KnightOnline.exe");
            if (!targetPid) targetPid = FindProcessByName(L"knightonline.exe");

            if (targetPid != 0)
            {
                Log(L"[+] 'KnightOnLine.exe' YAKALANDI! PID: %lu", targetPid);
                g_koPid = targetPid;
                if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);

                // Otomatik inject aktifse hemen baslat
                LRESULT checked = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (checked == BST_CHECKED)
                {
                    Log(L"[*] Otomatik Enjekte aktif. Zamanlama baslatiliyor...");
                    PerformInjectAsync(targetPid, dllPath);
                }
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        Log(L"[!] Zaman asimi: 60 saniye boyunca 'KnightOnLine.exe' gorulmedi.");
    }).detach();
}

// ==================== WIN32 UI ====================
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int x = 15, y = 15;

        // Knight Online Path
        CreateWindowExW(0, L"STATIC", L"KnightOnline.exe Yolu:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditKo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"C:\\NTTGame\\KnightOnlineEn\\KnightOnLine.exe",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 390, 20, hWnd, (HMENU)IDC_EDIT_KO_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 550, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_KO, nullptr, nullptr);

        y += 32;

        // DLL Path
        CreateWindowExW(0, L"STATIC", L"Enjekte DLL Yolu:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditDll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"berat.dll",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 390, 20, hWnd, (HMENU)IDC_EDIT_DLL_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 550, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_DLL, nullptr, nullptr);

        y += 32;

        // Launch Arguments (procID & nullparam cozumu)
        CreateWindowExW(0, L"STATIC", L"Baslatma Argumani:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        DWORD myPid = GetCurrentProcessId();
        wchar_t pidStr[32] = {};
        swprintf_s(pidStr, L"%lu", myPid);

        g_hEditArgs = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", pidStr,
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 80, 20, hWnd, (HMENU)IDC_EDIT_ARGS, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"(Gecerli canli procID parametresi !-(procID) ve -(nullparam) kontrollerini asar)",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x + 240, y + 2, 380, 18, hWnd, nullptr, nullptr, nullptr);

        y += 30;

        // Auto-Inject Checkbox
        g_hChkAutoInject = CreateWindowExW(0, L"BUTTON", L"Otomatik Enjekte Et (Oyun basladigi anda otomatik inject et)",
            WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, x + 150, y, 400, 20, hWnd, (HMENU)IDC_CHK_AUTO_INJECT, nullptr, nullptr);
        SendMessageW(g_hChkAutoInject, BM_SETCHECK, BST_CHECKED, 0);

        y += 35;

        // Action Buttons Row
        g_hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Oyunu Baslat",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x, y, 130, 32, hWnd, (HMENU)IDC_BTN_LAUNCH, nullptr, nullptr);

        g_hBtnLaunchLauncher = CreateWindowExW(0, L"BUTTON", L"Launcher ile Baslat",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 140, y, 140, 32, hWnd, (HMENU)IDC_BTN_LAUNCH_LAUNCHER, nullptr, nullptr);

        g_hBtnAttach = CreateWindowExW(0, L"BUTTON", L"Acik Oyuna Baglan",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 290, y, 135, 32, hWnd, (HMENU)IDC_BTN_ATTACH, nullptr, nullptr);

        g_hBtnInject = CreateWindowExW(0, L"BUTTON", L"Manuel Inject Et",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, x + 435, y, 120, 32, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Temizle",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 565, y, 65, 32, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, nullptr, nullptr);

        y += 45;

        CreateWindowExW(0, L"STATIC", L"Detayli Islem Loglari:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 150, 18, hWnd, nullptr, nullptr, nullptr);

        y += 20;
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            x, y, 630, 280, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);

        SendMessageW(g_hEditKo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hEditDll, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hEditArgs, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hChkAutoInject, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetXignLogCallback(Log);

        Log(L"UI Yuklendi. Hazir.");
        Log(L"[*] Canli procID (%lu) parametresi tanimlandi. !-(procID) hatasi cozuldu.", myPid);
        Log(L"[*] Tavsiye: 'Oyunu Baslat' ile veya 'Launcher ile Baslat' ile oyunu guvenle acabilirsiniz.");
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_BTN_BROWSE_KO)
        {
            auto path = BrowseFile(hWnd, L"Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0\0", L"KnightOnline.exe / Launcher.exe Sec");
            if (!path.empty())
            {
                SetWindowTextW(g_hEditKo, path.c_str());
                Log(L"[>] Hedef dosya secildi: %s", path.c_str());
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_BROWSE_DLL)
        {
            auto path = BrowseFile(hWnd, L"DLL Files (*.dll)\0*.dll\0All Files (*.*)\0*.*\0\0", L"DLL Sec");
            if (!path.empty())
            {
                SetWindowTextW(g_hEditDll, path.c_str());
                Log(L"[>] Enjekte edilecek DLL secildi: %s", path.c_str());
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_CLEAR_LOG)
        {
            SetWindowTextW(g_hLog, L"");
        }
        else if (LOWORD(wParam) == IDC_BTN_ATTACH)
        {
            Log(L"[*] Calisan 'KnightOnLine.exe' sureci araniyor...");
            DWORD pid = FindProcessByName(L"KnightOnLine.exe");
            if (pid == 0) pid = FindProcessByName(L"KnightOnline.exe");
            if (pid == 0) pid = FindProcessByName(L"knightonline.exe");

            if (pid != 0)
            {
                g_koPid = pid;
                Log(L"[+] Calisan Knight Online tespit edildi! PID: %lu", g_koPid);
                EnableWindow(g_hBtnInject, TRUE);

                wchar_t dllPath[MAX_PATH] = {};
                GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);

                LRESULT checked = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (checked == BST_CHECKED && *dllPath)
                {
                    Log(L"[*] Otomatik Enjekte devrede, enjeksiyon baslatiliyor...");
                    PerformInjectAsync(g_koPid, dllPath);
                }
            }
            else
            {
                Log(L"[-] 'KnightOnLine.exe' su anda calismiyor.");
                MessageBoxW(hWnd, L"KnightOnLine.exe bulunamadi! Once oyunu veya Launcher'i baslatin.", L"Bulunamadi", MB_ICONINFORMATION);
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_LAUNCH_LAUNCHER)
        {
            wchar_t koPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditKo, koPath, MAX_PATH);

            wchar_t workDir[MAX_PATH] = {};
            wcscpy_s(workDir, koPath);
            wchar_t* lastSlash = wcsrchr(workDir, L'\\');
            if (lastSlash) *lastSlash = 0;
            else GetCurrentDirectoryW(MAX_PATH, workDir);

            // Launcher.exe yolunu tespit et
            wchar_t launcherPath[MAX_PATH] = {};
            swprintf_s(launcherPath, L"%s\\Launcher.exe", workDir);

            DWORD attr = GetFileAttributesW(launcherPath);
            if (attr == INVALID_FILE_ATTRIBUTES)
            {
                Log(L"[-] HATA: Launcher.exe bulunamadi! Yol: %s", launcherPath);
                MessageBoxW(hWnd, L"Launcher.exe oyun klasorunde bulunamadi!", L"Hata", MB_ICONERROR);
                break;
            }

            wchar_t cmdLine[MAX_PATH * 2] = {};
            swprintf_s(cmdLine, L"\"%s\"", launcherPath);

            Log(L"==================================================");
            Log(L"[*] Resmi Launcher baslatiliyor...");
            Log(L"    -> Calistirilacak: %s", launcherPath);
            Log(L"    -> Calisma Dizini: %s", workDir);

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};

            if (CreateProcessW(launcherPath, cmdLine, nullptr, nullptr, FALSE, 0, nullptr, workDir, &si, &pi))
            {
                Log(L"[+] Launcher baslatildi! PID: %lu", pi.dwProcessId);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);

                wchar_t dllPath[MAX_PATH] = {};
                GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);

                // Oyun baslayana kadar arka planda izle
                StartProcessWatcher(dllPath);
            }
            else
            {
                Log(L"[-] Launcher CreateProcess HATA kodu: %lu", GetLastError());
            }
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

            wchar_t args[MAX_PATH] = {};
            GetWindowTextW(g_hEditArgs, args, MAX_PATH);
            if (wcslen(args) == 0)
            {
                DWORD myPid = GetCurrentProcessId();
                swprintf_s(args, L"%lu", myPid);
                SetWindowTextW(g_hEditArgs, args);
            }

            // Calisma dizini
            wchar_t workDir[MAX_PATH] = {};
            wcscpy_s(workDir, koPath);
            wchar_t* lastSlash = wcsrchr(workDir, L'\\');
            if (lastSlash) *lastSlash = 0;
            else GetCurrentDirectoryW(MAX_PATH, workDir);

            // procID parametresi ile komut satiri
            wchar_t cmdLine[MAX_PATH * 3] = {};
            swprintf_s(cmdLine, L"\"%s\" %s", koPath, args);

            Log(L"==================================================");
            Log(L"[*] CreateProcess: %s", koPath);
            Log(L"    -> Calisma Dizini       : %s", workDir);
            Log(L"    -> Komut Satiri         : %s", cmdLine);
            Log(L"    -> Gecilen procID       : %s (!-(procID) kontrolunu asar)", args);

            STARTUPINFOW si = { sizeof(si) };
            PROCESS_INFORMATION pi = {};

            BOOL success = CreateProcessW(
                koPath,
                cmdLine,
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                workDir,
                &si,
                &pi
            );

            if (success)
            {
                g_koPid = pi.dwProcessId;
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);

                Log(L"[+] Oyun baslatildi. PID: %lu", g_koPid);
                EnableWindow(g_hBtnInject, TRUE);

                wchar_t dllPath[MAX_PATH] = {};
                GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);

                LRESULT checked = SendMessageW(g_hChkAutoInject, BM_GETCHECK, 0, 0);
                if (checked == BST_CHECKED && *dllPath)
                {
                    Log(L"[*] Otomatik Enjekte aktif. Zamanlama motoru baslatiliyor...");
                    PerformInjectAsync(g_koPid, dllPath);
                }
                else
                {
                    Log(L"[*] Hazir. Enjeksiyon icin 'Manuel Inject Et' butonuna tiklayabilirsiniz.");
                }
            }
            else
            {
                DWORD err = GetLastError();
                Log(L"[-] CreateProcess HATA kodu: %lu", err);
                MessageBoxW(hWnd, L"Oyun baslatilamadi! Dosya yolunu kontrol edin.", L"Hata", MB_ICONERROR);
            }
        }
        else if (LOWORD(wParam) == IDC_BTN_INJECT)
        {
            if (g_koPid == 0)
            {
                MessageBoxW(hWnd, L"Once oyunu baslatin veya 'Acik Oyuna Baglan' secenegini kullanin.", L"Uyari", MB_ICONWARNING);
                break;
            }

            wchar_t dllPath[MAX_PATH] = {};
            GetWindowTextW(g_hEditDll, dllPath, MAX_PATH);
            if (!*dllPath)
            {
                MessageBoxW(hWnd, L"Lutfen enjekte edilecek DLL yolunu belirtin.", L"Uyari", MB_ICONWARNING);
                break;
            }

            PerformInjectAsync(g_koPid, dllPath);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, message, wParam, lParam);
}

// ==================== ENTRY POINT ====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    const wchar_t CLASS_NAME[] = L"BlackboneKnightInjectorWnd";

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"Knight Online XignCode3 Injector - BlackBone",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 680, 560,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!hWnd) return 0;

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
