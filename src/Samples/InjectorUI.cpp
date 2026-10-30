// InjectorUI.cpp
// Blackbone tabanli Knight Online XignCode3 Bypass Injector
// Detayli Loglama + Nullparam Cozumu + Win32 UI + Timing Manual Map

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
#define IDC_EDIT_KO_PATH    101
#define IDC_BTN_BROWSE_KO   102
#define IDC_EDIT_DLL_PATH   103
#define IDC_BTN_BROWSE_DLL  104
#define IDC_EDIT_ARGS       105
#define IDC_BTN_LAUNCH      106
#define IDC_BTN_ATTACH      107
#define IDC_BTN_INJECT      108
#define IDC_BTN_CLEAR_LOG   109
#define IDC_LOG             110

// ==================== GLOBALS ====================
HWND g_hEditKo = nullptr;
HWND g_hEditDll = nullptr;
HWND g_hEditArgs = nullptr;
HWND g_hBtnLaunch = nullptr;
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
        Log(L"==================================================");
        Log(L"[*] ENJEKSIYON ZAMANLAMASI BASLATILDI (PID: %lu)", pid);
        Log(L"[*] Hedef DLL Dosyasi: %s", dllPath.c_str());

        // DLL dosyasini kontrol et
        DWORD attr = GetFileAttributesW(dllPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            Log(L"[-] HATA: DLL dosyasi bulunamadi! Yol: %s", dllPath.c_str());
            if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
            return;
        }

        HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            LARGE_INTEGER sz = {};
            if (GetFileSizeEx(hFile, &sz))
            {
                Log(L"[+] DLL boyutu dogrulandi: %lld bytes", sz.QuadPart);
            }
            CloseHandle(hFile);
        }

        Process proc;
        Log(L"[*] BlackBone Process::Attach(PID: %lu) cagriliyor...", pid);
        NTSTATUS st = proc.Attach(pid);
        if (!NT_SUCCESS(st))
        {
            Log(L"[-] HATA: Surece baglanilamadi! NTSTATUS: 0x%08X (GetLastError: %lu)", st, GetLastError());
            if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
            return;
        }
        Log(L"[+] Surece basariyla baglanildi. Handle: 0x%p", proc.core().handle());

        // Ana modul kontrolu
        Log(L"[*] Hedef ana modul ('KnightOnLine.exe') kontrol ediliyor...");
        if (!WaitForModuleXign(proc, L"KnightOnLine.exe", 10000))
        {
            Log(L"[!] UYARI: KnightOnLine.exe ana modul zaman asimi, isleme devam ediliyor.");
        }
        else
        {
            auto koMod = proc.modules().GetModule(L"KnightOnLine.exe");
            if (koMod)
            {
                Log(L"[+] Ana modul bulundu: Base=0x%p, Boyut=0x%X", (void*)koMod->baseAddress, koMod->size);
            }
        }

        // ---- TIMING 1: x3.xem bekle (max 30sn) ----
        Log(L"[*] TIMING 1: XignCode (x3.xem) modulu bekleniyor (Limit: 30 sn)...");
        bool x3Found = WaitForModuleXign(proc, L"x3.xem", 30000);
        if (!x3Found)
        {
            Log(L"[!] UYARI: x3.xem belirlenen surede gorulmedi. Enjeksiyon yine de surduruluyor...");
        }
        else
        {
            auto x3mod = proc.modules().GetModule(L"x3.xem");
            if (x3mod)
            {
                Log(L"[+] x3.xem modulu tespit edildi! Base=0x%p, Boyut=0x%X", (void*)x3mod->baseAddress, x3mod->size);
            }
        }

        // ---- TIMING 2: Sureci dondur ----
        Log(L"[*] TIMING 2: Surec donduruluyor (NtSuspendProcess)...");
        if (!SuspendProcessXign(proc))
        {
            Log(L"[-] HATA: NtSuspendProcess basarisiz!");
            if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
            return;
        }
        Log(L"[+] Surec basariyla donduruldu (Thread'ler askiya alindi).");

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // ---- TIMING 3: x3.xem PEB unlink ----
        if (x3Found)
        {
            auto x3mod = proc.modules().GetModule(L"x3.xem");
            if (x3mod)
            {
                uintptr_t x3base = static_cast<uintptr_t>(x3mod->baseAddress);
                Log(L"[*] TIMING 3: x3.xem (Base: 0x%p) PEB listelerinden unlinked yapiliyor...", (void*)x3base);
                bool unlinked = RemoteUnlinkModule(proc, x3base, L"x3.xem");
                if (unlinked)
                    Log(L"[+] x3.xem PEB listelerinden (InLoad, InMemory, InInit) basariyla cikarildi.");
                else
                    Log(L"[!] x3.xem PEB listesinde bulunamadi veya zaten unlinked edilmis.");
            }
        }

        // ---- TIMING 4: Manual Map inject ----
        Log(L"[*] TIMING 4: Manual Map enjeksiyonu baslatiliyor...");
        Log(L"    -> Bayraklar: WipeHeader (PE Header Sil) | HideVAD (Bellek Sayfasini Gizle)");

        auto image = proc.mmap().MapImage(
            dllPath.c_str(),
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
        Log(L"    -> Modul Taban Adresi (ImageBase) : 0x%p", (void*)imgBase);
        Log(L"    -> Modul Boyutu                   : 0x%X (%u bytes)", modInfo->size, modInfo->size);

        // ---- TIMING 5: DLL PEB'den gizle ----
        Log(L"[*] TIMING 5: Guvenlik beklemesi yapiliyor (1500 ms)...");
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        Log(L"[*] %s modulu PEB listelerinden temizleniyor (Base: 0x%p)...", dllPath.c_str(), (void*)imgBase);
        RemoteUnlinkModule(proc, imgBase, L"berat.dll");
        Log(L"[+] PEB temizleme islemi tamamlandi.");

        // ---- TIMING 6: Sureci devam ettir ----
        Log(L"[*] TIMING 6: Surec devam ettiriliyor (NtResumeProcess)...");
        if (ResumeProcessXign(proc))
        {
            Log(L"[+] NtResumeProcess basarili! Hedef oyun akisina devam ediyor.");
        }
        else
        {
            Log(L"[!] UYARI: NtResumeProcess sirasinda hata olustu.");
        }

        proc.Detach();
        Log(L"[*] BlackBone Process baglantisi kesildi (Detach).");
        Log(L"[OK] ==================================================");
        Log(L"[OK] ENJEKSIYON VE GIZLEME ISLEMI BASARIYLA TAMAMLANDI!");
        Log(L"[OK] ==================================================");

        if (g_hBtnInject) EnableWindow(g_hBtnInject, TRUE);
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
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 360, 20, hWnd, (HMENU)IDC_EDIT_KO_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 520, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_KO, nullptr, nullptr);

        y += 35;

        // DLL Path
        CreateWindowExW(0, L"STATIC", L"Enjekte DLL Yolu:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditDll = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"berat.dll",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 360, 20, hWnd, (HMENU)IDC_EDIT_DLL_PATH, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"...",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 520, y - 1, 40, 22, hWnd, (HMENU)IDC_BTN_BROWSE_DLL, nullptr, nullptr);

        y += 35;

        // Launch Arguments (Nullparam Bypass)
        CreateWindowExW(0, L"STATIC", L"Baslatma Argumani:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 140, 18, hWnd, nullptr, nullptr, nullptr);

        g_hEditArgs = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1",
            WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, x + 150, y, 60, 20, hWnd, (HMENU)IDC_EDIT_ARGS, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"(Varsayilan '1' nullparam hatasini cozer)",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x + 220, y + 2, 340, 18, hWnd, nullptr, nullptr, nullptr);

        y += 45;

        // Action Buttons
        g_hBtnLaunch = CreateWindowExW(0, L"BUTTON", L"Oyunu Baslat",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 20, y, 120, 30, hWnd, (HMENU)IDC_BTN_LAUNCH, nullptr, nullptr);

        g_hBtnAttach = CreateWindowExW(0, L"BUTTON", L"Acik Oyuna Baglan",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 155, y, 130, 30, hWnd, (HMENU)IDC_BTN_ATTACH, nullptr, nullptr);

        g_hBtnInject = CreateWindowExW(0, L"BUTTON", L"Inject Et",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON | WS_DISABLED, x + 295, y, 120, 30, hWnd, (HMENU)IDC_BTN_INJECT, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Log Temizle",
            WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, x + 465, y, 95, 30, hWnd, (HMENU)IDC_BTN_CLEAR_LOG, nullptr, nullptr);

        y += 45;

        CreateWindowExW(0, L"STATIC", L"Detayli Islem Loglari:",
            WS_VISIBLE | WS_CHILD | SS_LEFT, x, y, 150, 18, hWnd, nullptr, nullptr, nullptr);

        y += 20;
        g_hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            x, y, 560, 280, hWnd, (HMENU)IDC_LOG, nullptr, nullptr);

        SendMessageW(g_hEditKo, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hEditDll, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hEditArgs, WM_SETFONT, (WPARAM)hFont, TRUE);
        SendMessageW(g_hLog, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetXignLogCallback(Log);

        Log(L"UI Yuklendi. Knight Online path ve DLL secip 'Oyunu Baslat' tikla.");
        Log(L"[*] Parametre '1' aktif: 'Please execute Knight online via launcher -(nullparam)' engellendi.");
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_BTN_BROWSE_KO)
        {
            auto path = BrowseFile(hWnd, L"Executables (*.exe)\0*.exe\0All Files (*.*)\0*.*\0\0", L"KnightOnline.exe Sec");
            if (!path.empty())
            {
                SetWindowTextW(g_hEditKo, path.c_str());
                Log(L"[>] Knight Online secildi: %s", path.c_str());
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

            if (pid != 0)
            {
                g_koPid = pid;
                Log(L"[+] Calisan Knight Online tespit edildi! PID: %lu", g_koPid);
                EnableWindow(g_hBtnInject, TRUE);
            }
            else
            {
                Log(L"[-] 'KnightOnLine.exe' su anda calismiyor.");
                MessageBoxW(hWnd, L"KnightOnLine.exe bulunamadi! Once oyunu baslatin.", L"Bulunamadi", MB_ICONINFORMATION);
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
                wcscpy_s(args, L"1");
                SetWindowTextW(g_hEditArgs, L"1");
            }

            // Calisma dizini (Working directory)
            wchar_t workDir[MAX_PATH] = {};
            wcscpy_s(workDir, koPath);
            wchar_t* lastSlash = wcsrchr(workDir, L'\\');
            if (lastSlash) 
                *lastSlash = 0;
            else 
                GetCurrentDirectoryW(MAX_PATH, workDir);

            // Nullparam cozumu: Komut satirina "KnightOnLine.exe" 1 parametresi eklenir
            wchar_t cmdLine[MAX_PATH * 3] = {};
            swprintf_s(cmdLine, L"\"%s\" %s", koPath, args);

            Log(L"==================================================");
            Log(L"[*] CreateProcess: %s", koPath);
            Log(L"    -> Calisma Dizini       : %s", workDir);
            Log(L"    -> Komut Satiri (Param) : %s", cmdLine);
            Log(L"    -> Parametre            : %s (nullparam bypass)", args);

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
                Log(L"[*] 'Inject Et' butonu aktiflestirildi.");
                EnableWindow(g_hBtnInject, TRUE);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 615, 520,
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
