#pragma once
// MemoryReaderUI.hpp
// Inject edildikten sonra oyun process icinde calisir.
// CreateMemoryReaderWindow() cagrisi Win32 penceresi acar;
// kullanici adres + tip + boyut girince o bellek alanini okur.

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace MemUI
{
    // -------------------------------------------------------------------------
    // Control IDs
    // -------------------------------------------------------------------------
    static constexpr int ID_EDIT_ADDR    = 101;
    static constexpr int ID_EDIT_SIZE    = 102;
    static constexpr int ID_COMBO_TYPE   = 103;
    static constexpr int ID_BTN_READ     = 104;
    static constexpr int ID_BTN_WATCH    = 105;
    static constexpr int ID_BTN_CLEAR    = 106;
    static constexpr int ID_LIST_RESULT  = 107;
    static constexpr int ID_STATIC_STATUS= 108;
    static constexpr int ID_BTN_EXPORT   = 109;
    static constexpr int ID_EDIT_MODULE  = 110;
    static constexpr int ID_BTN_MODULES  = 111;
    static constexpr int ID_TIMER_WATCH  = 1001;

    static constexpr UINT WM_APPEND_LOG  = WM_APP + 10;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    static inline void SetStatus(HWND hWnd, const wchar_t* text)
    {
        SetDlgItemTextW(hWnd, ID_STATIC_STATUS, text);
    }

    static inline void AppendList(HWND hList, const wchar_t* line)
    {
        int idx = (int)SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)line);
        SendMessageW(hList, LB_SETCURSEL, idx, 0);
    }

    // -------------------------------------------------------------------------
    // SafeRead — VirtualQuery + SEH korumal?
    // -------------------------------------------------------------------------
    static bool SafeRead(uintptr_t address, void* buffer, size_t size)
    {
        MEMORY_BASIC_INFORMATION mbi = {};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        __try
        {
            memcpy(buffer, reinterpret_cast<const void*>(address), size);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // ReadAndFormat — adres+boyutu oku, tip'e gore formatlayarak wstring dondur
    // -------------------------------------------------------------------------
    enum class ReadType { Hex, Int32, Int64, Float, Double, Utf8, Utf16 };

    static std::wstring FormatRead(uintptr_t address, size_t size, ReadType type)
    {
        if (size == 0 || size > 0x10000)
            return L"[Hata] Boyut 1-65536 araliginda olmali.";

        std::vector<uint8_t> buf(size, 0);
        if (!SafeRead(address, buf.data(), size))
            return L"[Hata] Adres okunamadi (ACCESS DENIED / NOT COMMITTED / GUARD).";

        wchar_t line[512];
        switch (type)
        {
        case ReadType::Int32:
            if (size >= 4)
            {
                int32_t v; memcpy(&v, buf.data(), 4);
                swprintf_s(line, L"Int32: %d (0x%08X)", v, (uint32_t)v);
            }
            else swprintf_s(line, L"[Hata] Int32 icin en az 4 byte gerekli.");
            break;

        case ReadType::Int64:
            if (size >= 8)
            {
                int64_t v; memcpy(&v, buf.data(), 8);
                swprintf_s(line, L"Int64: %lld (0x%016llX)", v, (uint64_t)v);
            }
            else swprintf_s(line, L"[Hata] Int64 icin en az 8 byte gerekli.");
            break;

        case ReadType::Float:
            if (size >= 4)
            {
                float v; memcpy(&v, buf.data(), 4);
                swprintf_s(line, L"Float: %.6f (raw: 0x%08X)", v, *(uint32_t*)buf.data());
            }
            else swprintf_s(line, L"[Hata] Float icin en az 4 byte gerekli.");
            break;

        case ReadType::Double:
            if (size >= 8)
            {
                double v; memcpy(&v, buf.data(), 8);
                swprintf_s(line, L"Double: %.10g (raw: 0x%016llX)", v, *(uint64_t*)buf.data());
            }
            else swprintf_s(line, L"[Hata] Double icin en az 8 byte gerekli.");
            break;

        case ReadType::Utf8:
        {
            buf.push_back(0);
            int needed = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)buf.data(), -1, nullptr, 0);
            std::wstring ws(needed, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, (LPCCH)buf.data(), -1, ws.data(), needed);
            swprintf_s(line, L"UTF-8: \"%.*s\"", 200, ws.c_str());
            break;
        }
        case ReadType::Utf16:
        {
            size_t chars = size / 2;
            std::wstring ws(reinterpret_cast<const wchar_t*>(buf.data()), chars);
            swprintf_s(line, L"UTF-16: \"%.*s\"", 200, ws.c_str());
            break;
        }

        default: // Hex dump
        {
            std::wstring hex;
            hex.reserve(size * 3 + 64);
            wchar_t tmp[8];
            for (size_t i = 0; i < size; ++i)
            {
                if (i && (i % 16) == 0) hex += L"\r\n          ";
                swprintf_s(tmp, L"%02X ", buf[i]);
                hex += tmp;
            }
            swprintf_s(line, L"Hex [%zu byte]:", size);
            return std::wstring(line) + L"\r\n" + hex;
        }
        }
        return line;
    }

    // -------------------------------------------------------------------------
    // ListModules — process'teki yüklü modülleri listele
    // -------------------------------------------------------------------------
    static void ListModulesIntoListbox(HWND hList)
    {
        SendMessageW(hList, LB_RESETCONTENT, 0, 0);
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        if (hSnap == INVALID_HANDLE_VALUE) return;

        MODULEENTRY32W me = { sizeof(me) };
        if (Module32FirstW(hSnap, &me))
        {
            do {
                wchar_t buf[512];
                swprintf_s(buf, L"0x%016llX  %-12lu  %s",
                    (uint64_t)(uintptr_t)me.modBaseAddr,
                    me.modBaseSize,
                    me.szModule);
                AppendList(hList, buf);
            } while (Module32NextW(hSnap, &me));
        }
        CloseHandle(hSnap);
    }

    // -------------------------------------------------------------------------
    // Window state
    // -------------------------------------------------------------------------
    struct WndState
    {
        bool watchActive = false;
        uintptr_t watchAddress = 0;
        size_t    watchSize = 4;
        ReadType  watchType = ReadType::Int32;
    };
    static WndState g_state;

    // -------------------------------------------------------------------------
    // DoRead — UI'dan adres okuyup listeye ekle
    // -------------------------------------------------------------------------
    static void DoRead(HWND hWnd, HWND hList)
    {
        wchar_t addrBuf[64] = {}, sizeBuf[32] = {};
        GetDlgItemTextW(hWnd, ID_EDIT_ADDR, addrBuf, 64);
        GetDlgItemTextW(hWnd, ID_EDIT_SIZE, sizeBuf, 32);

        if (!addrBuf[0]) { SetStatus(hWnd, L"Adres bos!"); return; }

        uintptr_t address = (uintptr_t)wcstoull(addrBuf, nullptr, 16);
        size_t    size    = sizeBuf[0] ? (size_t)wcstoul(sizeBuf, nullptr, 10) : 4;
        if (size == 0) size = 4;

        int typeIdx = (int)SendDlgItemMessageW(hWnd, ID_COMBO_TYPE, CB_GETCURSEL, 0, 0);
        ReadType type = (typeIdx >= 0 && typeIdx <= (int)ReadType::Utf16)
            ? (ReadType)typeIdx : ReadType::Hex;

        wchar_t header[128];
        swprintf_s(header, L"─── Read 0x%llX (%zu byte) ───", (uint64_t)address, size);
        AppendList(hList, header);

        std::wstring result = FormatRead(address, size, type);

        // Multi-line result → add each line separately
        size_t pos = 0, next;
        while ((next = result.find(L"\r\n", pos)) != std::wstring::npos)
        {
            AppendList(hList, result.substr(pos, next - pos).c_str());
            pos = next + 2;
        }
        if (pos < result.size())
            AppendList(hList, result.substr(pos).c_str());

        wchar_t st[128];
        swprintf_s(st, L"Okundu: 0x%llX (%zu byte)", (uint64_t)address, size);
        SetStatus(hWnd, st);
    }

    // -------------------------------------------------------------------------
    // Window Proc
    // -------------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        HWND hList = GetDlgItem(hWnd, ID_LIST_RESULT);

        switch (msg)
        {
        case WM_CREATE:
        {
            HFONT hFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

            // Address label + input
            CreateWindowW(L"STATIC", L"Adres (hex):", WS_CHILD | WS_VISIBLE,
                8, 10, 100, 18, hWnd, nullptr, nullptr, nullptr);
            HWND hAddr = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                110, 8, 200, 22, hWnd, (HMENU)(INT_PTR)ID_EDIT_ADDR, nullptr, nullptr);
            SendMessageW(hAddr, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Size label + input
            CreateWindowW(L"STATIC", L"Boyut (byte):", WS_CHILD | WS_VISIBLE,
                320, 10, 100, 18, hWnd, nullptr, nullptr, nullptr);
            HWND hSize = CreateWindowW(L"EDIT", L"4", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                425, 8, 60, 22, hWnd, (HMENU)(INT_PTR)ID_EDIT_SIZE, nullptr, nullptr);
            SendMessageW(hSize, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Type combo
            CreateWindowW(L"STATIC", L"Tip:", WS_CHILD | WS_VISIBLE,
                495, 10, 30, 18, hWnd, nullptr, nullptr, nullptr);
            HWND hCombo = CreateWindowW(L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                530, 6, 100, 120, hWnd, (HMENU)(INT_PTR)ID_COMBO_TYPE, nullptr, nullptr);
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Hex");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Int32");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Int64");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Float");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Double");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"UTF-8");
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"UTF-16");
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

            // Buttons row 1
            CreateWindowW(L"BUTTON", L"Oku (F5)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                640, 6, 90, 26, hWnd, (HMENU)(INT_PTR)ID_BTN_READ, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Watch", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                738, 6, 70, 26, hWnd, (HMENU)(INT_PTR)ID_BTN_WATCH, nullptr, nullptr);

            // Module filter
            CreateWindowW(L"STATIC", L"Modul:", WS_CHILD | WS_VISIBLE,
                8, 40, 50, 18, hWnd, nullptr, nullptr, nullptr);
            HWND hMod = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                60, 38, 200, 22, hWnd, (HMENU)(INT_PTR)ID_EDIT_MODULE, nullptr, nullptr);
            SendMessageW(hMod, WM_SETFONT, (WPARAM)hFont, TRUE);
            CreateWindowW(L"BUTTON", L"Modulleri Listele", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                270, 37, 140, 24, hWnd, (HMENU)(INT_PTR)ID_BTN_MODULES, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Temizle", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                420, 37, 70, 24, hWnd, (HMENU)(INT_PTR)ID_BTN_CLEAR, nullptr, nullptr);
            CreateWindowW(L"BUTTON", L"Disa Aktar", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                500, 37, 90, 24, hWnd, (HMENU)(INT_PTR)ID_BTN_EXPORT, nullptr, nullptr);

            // Listbox (result area)
            HWND hLB = CreateWindowW(L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | WS_HSCROLL |
                LBS_NOINTEGRALHEIGHT | LBS_NOSEL,
                8, 70, 994, 430, hWnd, (HMENU)(INT_PTR)ID_LIST_RESULT, nullptr, nullptr);
            SendMessageW(hLB, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Status bar
            CreateWindowW(L"STATIC", L"Hazir.",
                WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                8, 508, 994, 20, hWnd, (HMENU)(INT_PTR)ID_STATIC_STATUS, nullptr, nullptr);

            // Initial info
            wchar_t info[256];
            swprintf_s(info, L"[INFO] PID=%lu  Process: ", GetCurrentProcessId());
            wchar_t pname[MAX_PATH];
            GetModuleFileNameW(nullptr, pname, MAX_PATH);
            wcscat_s(info, pname);
            AppendList(hLB, info);
            AppendList(hLB, L"[INFO] Adres alanina 0x hex degeri girin, 'Oku' ile okuyun.");
            AppendList(hLB, L"[INFO] 'Modulleri Listele' ile yuklü modulleri ve base adreslerini gorun.");

            return 0;
        }

        case WM_KEYDOWN:
            if (wParam == VK_F5)
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_READ, BN_CLICKED), 0);
            break;

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);

            if (id == ID_BTN_READ)
            {
                DoRead(hWnd, hList);
            }
            else if (id == ID_BTN_WATCH)
            {
                if (!g_state.watchActive)
                {
                    // Activate watch
                    wchar_t addrBuf[64] = {}, sizeBuf[32] = {};
                    GetDlgItemTextW(hWnd, ID_EDIT_ADDR, addrBuf, 64);
                    GetDlgItemTextW(hWnd, ID_EDIT_SIZE, sizeBuf, 32);
                    if (!addrBuf[0]) { SetStatus(hWnd, L"Watch icin adres girin!"); break; }

                    g_state.watchAddress = (uintptr_t)wcstoull(addrBuf, nullptr, 16);
                    g_state.watchSize    = sizeBuf[0] ? (size_t)wcstoul(sizeBuf, nullptr, 10) : 4;
                    int typeIdx = (int)SendDlgItemMessageW(hWnd, ID_COMBO_TYPE, CB_GETCURSEL, 0, 0);
                    g_state.watchType    = (typeIdx >= 0) ? (ReadType)typeIdx : ReadType::Int32;
                    g_state.watchActive  = true;

                    SetTimer(hWnd, ID_TIMER_WATCH, 500, nullptr);
                    SetDlgItemTextW(hWnd, ID_BTN_WATCH, L"Stop");
                    SetStatus(hWnd, L"Watch aktif (500ms)");
                }
                else
                {
                    KillTimer(hWnd, ID_TIMER_WATCH);
                    g_state.watchActive = false;
                    SetDlgItemTextW(hWnd, ID_BTN_WATCH, L"Watch");
                    SetStatus(hWnd, L"Watch durduruldu.");
                }
            }
            else if (id == ID_BTN_CLEAR)
            {
                SendMessageW(hList, LB_RESETCONTENT, 0, 0);
                SetStatus(hWnd, L"Liste temizlendi.");
            }
            else if (id == ID_BTN_MODULES)
            {
                SendMessageW(hList, LB_RESETCONTENT, 0, 0);
                AppendList(hList, L"─── Yuklu Moduller ───");
                AppendList(hList, L"Base Adresi            Boyut         Isim");
                AppendList(hList, L"───────────────────────────────────────────────────");
                ListModulesIntoListbox(hList);
                SetStatus(hWnd, L"Modul listesi guncellendi.");
            }
            else if (id == ID_BTN_EXPORT)
            {
                int count = (int)SendMessageW(hList, LB_GETCOUNT, 0, 0);
                if (count <= 0) { SetStatus(hWnd, L"Liste bos."); break; }

                wchar_t path[MAX_PATH];
                GetTempPathW(MAX_PATH, path);
                wcscat_s(path, L"berat_memread.txt");

                HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hFile == INVALID_HANDLE_VALUE) { SetStatus(hWnd, L"Dosya olusturulamadi."); break; }

                for (int i = 0; i < count; ++i)
                {
                    int len = (int)SendMessageW(hList, LB_GETTEXTLEN, i, 0);
                    if (len <= 0) continue;
                    std::wstring line(len + 1, L'\0');
                    SendMessageW(hList, LB_GETTEXT, i, (LPARAM)line.data());
                    // Write as UTF-16 LE line
                    DWORD written;
                    WriteFile(hFile, line.c_str(), (DWORD)(len * sizeof(wchar_t)), &written, nullptr);
                    WriteFile(hFile, L"\r\n", 4, &written, nullptr);
                }
                CloseHandle(hFile);

                wchar_t st[MAX_PATH + 64];
                swprintf_s(st, L"Kaydedildi: %s", path);
                SetStatus(hWnd, st);
            }
            break;
        }

        case WM_TIMER:
            if (wParam == ID_TIMER_WATCH && g_state.watchActive)
            {
                wchar_t header[128];
                swprintf_s(header, L"[Watch] 0x%llX", (uint64_t)g_state.watchAddress);
                std::wstring result = FormatRead(g_state.watchAddress, g_state.watchSize, g_state.watchType);
                // Single line for watch mode
                size_t nl = result.find(L"\r\n");
                if (nl != std::wstring::npos) result = result.substr(0, nl) + L" ...";
                wchar_t full[512];
                swprintf_s(full, L"%s  =>  %s", header, result.c_str());
                AppendList(hList, full);
            }
            break;

        case WM_DESTROY:
            KillTimer(hWnd, ID_TIMER_WATCH);
            PostQuitMessage(0);
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------
    static DWORD WINAPI MemReaderThreadProc(LPVOID)
    {
        HMODULE hInst = GetModuleHandleW(nullptr);

        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"BeratMemReader";
        wc.hIcon         = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassExW(&wc);

        HWND hWnd = CreateWindowExW(
            0, L"BeratMemReader",
            L"berat.dll  –  Memory Reader  [inject edilmis]",
            WS_OVERLAPPEDWINDOW,
            100, 100, 1024, 580,
            nullptr, nullptr, hInst, nullptr);

        if (!hWnd) return 1;
        ShowWindow(hWnd, SW_SHOWNORMAL);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0))
        {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_F5)
                SendMessageW(hWnd, WM_COMMAND, MAKEWPARAM(ID_BTN_READ, BN_CLICKED), 0);
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }

    // Bunu dllmain.cpp'den cagiriniz (WorkerThread sonunda veya hemen).
    inline void CreateMemoryReaderWindow()
    {
        HANDLE hT = CreateThread(nullptr, 0, MemReaderThreadProc, nullptr, 0, nullptr);
        if (hT) CloseHandle(hT);
    }

} // namespace MemUI
