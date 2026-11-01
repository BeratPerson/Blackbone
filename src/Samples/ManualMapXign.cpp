#include "ManualMapXign.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <tlhelp32.h>
#include <winternl.h>
#include <vector>

using namespace blackbone;

// -----------------------------------------------------------------------
// SeLoadDriverPrivilege'i enable et — SCM driver yukleme icin zorunlu
// -----------------------------------------------------------------------
static bool EnableLoadDriverPrivilege()
{
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LUID luid = {};
    if (!LookupPrivilegeValueW(nullptr, L"SeLoadDriverPrivilege", &luid))
    {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount            = 1;
    tp.Privileges[0].Luid        = luid;
    tp.Privileges[0].Attributes  = SE_PRIVILEGE_ENABLED;

    BOOL ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    DWORD err = GetLastError();
    CloseHandle(hToken);
    return ok && (err == ERROR_SUCCESS);
}

// -----------------------------------------------------------------------
// WDAC Vulnerable Driver Blocklist'i devre disi birak
// STATUS_DRIVER_BLOCKED_CRITICAL (0xC0000906) hatasinin cozumu.
// NOT: HVCI (Hypervisor-Protected Code Integrity) AKTIF ise bu yetmez;
//      BIOS'tan Secure Boot + VBS/HVCI kapatmak gerekir.
// Reboot gerekir — bir sonraki acilista etkin olur.
// -----------------------------------------------------------------------
static bool DisableWdacDriverBlocklist()
{
    HKEY hKey = nullptr;
    LONG res = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\CI\\Config",
        0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &hKey, nullptr);
    if (res != ERROR_SUCCESS) return false;

    // Mevcut degeri kontrol et
    DWORD curVal = 1, type = REG_DWORD, sz = sizeof(DWORD);
    RegQueryValueExW(hKey, L"VulnerableDriverBlocklistEnable", nullptr, &type, (LPBYTE)&curVal, &sz);

    if (curVal == 0)
    {
        RegCloseKey(hKey);
        return true; // Zaten devre disi
    }

    DWORD val = 0;
    res = RegSetValueExW(hKey, L"VulnerableDriverBlocklistEnable",
                          0, REG_DWORD, (const BYTE*)&val, sizeof(val));
    RegCloseKey(hKey);
    return res == ERROR_SUCCESS;
}

// Forward declaration (InternalLog asagida tanimli)
static void InternalLog(const wchar_t* fmt, ...);

// -----------------------------------------------------------------------
// Test Signing'i BCDEdit ile otomatik aktif et.
// NOT: Degisiklik bir sonraki REBOOT'ta etkin olur.
// -----------------------------------------------------------------------
static void TryEnableTestSigning()
{
    // Mevcut durumu kontrol et — CI policy flags (0x08 = testsigning aktif)
    DWORD flags = 0, type = REG_DWORD, sz = sizeof(DWORD);
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\CI\\Config",
                      0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"TestSigning", nullptr, &type, (LPBYTE)&flags, &sz);
        RegCloseKey(hKey);
    }
    // CI\Config\TestSigning var ve 1 ise zaten aktif
    if (flags == 1)
    {
        InternalLog(L"[*] TestSigning registry zaten aktif — reboot bekleniyor olabilir.");
        return;
    }

    InternalLog(L"[*] 'bcdedit /set testsigning on' otomatik calistirilıyor...");

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    wchar_t cmd[] = L"bcdedit /set testsigning on";

    if (CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        WaitForSingleObject(pi.hProcess, 8000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode == 0)
        {
            InternalLog(L"[+] Test Signing AKTIF edildi!");
            InternalLog(L"[!] >>> Bilgisayari YENIDEN BASLATMANIZ GEREKIYOR <<<");
            InternalLog(L"[!]     Reboot sonrasi Samples.exe tekrar calistirin.");
        }
        else
        {
            InternalLog(L"[!] bcdedit basarisiz (ExitCode=%lu) — Admin yetki yok olabilir.", exitCode);
            InternalLog(L"[!] Manuel calistirin (Admin CMD): bcdedit /set testsigning on");
            InternalLog(L"[!] Sonra bilgisayari YENIDEN BASLATIP Samples.exe tekrar calistirin.");
        }
    }
    else
    {
        DWORD err = GetLastError();
        InternalLog(L"[!] bcdedit bulunamadi veya calistirilamadi (Win32=%lu).", err);
        InternalLog(L"[!] Manuel: Admin CMD > bcdedit /set testsigning on > REBOOT");
    }
}

// -----------------------------------------------------------------------
// {60FD87F8} WDAC Platform Politikasini devre disi birak
// KB5077181 ile gelen bu Microsoft-imzali politika WDAC seviyesinde
// test-signed driver'lari blokluyor. nointegritychecks bunu bypass ETMEZ.
// Bu fonksiyon dosyayi rename eder (reboot'ta etkili).
// -----------------------------------------------------------------------
static void TryDisableWdacPlatformPolicy()
{
    const wchar_t* cipPaths[] = {
        L"C:\\Windows\\System32\\CodeIntegrity\\CiPolicies\\Active\\{60FD87F8-4593-44A0-91B0-2E0DA022F248}.cip",
        L"C:\\Windows\\System32\\CodeIntegrity\\CIPolicies\\Active\\{60FD87F8-4593-44A0-91B0-2E0DA022F248}.cip",
    };

    for (const auto* cip : cipPaths)
    {
        if (GetFileAttributesW(cip) == INVALID_FILE_ATTRIBUTES)
            continue;

        InternalLog(L"[*] {60FD87F8} WDAC politikasi bulundu: %s", cip);

        // Sahiplik al
        {
            HANDLE hToken = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &hToken))
            {
                TOKEN_PRIVILEGES tp = {};
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                LookupPrivilegeValueW(nullptr, L"SeTakeOwnershipPrivilege", &tp.Privileges[0].Luid);
                AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
                LookupPrivilegeValueW(nullptr, L"SeRestorePrivilege", &tp.Privileges[0].Luid);
                AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
                CloseHandle(hToken);
            }
        }

        // Readonly kaldir
        DWORD attr = GetFileAttributesW(cip);
        if (attr & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(cip, attr & ~FILE_ATTRIBUTE_READONLY);

        // Rename to .disabled (mevcut boot'ta WDAC bellekte kalir ama sonraki boot'ta yuklenmez)
        wchar_t disabledPath[MAX_PATH] = {};
        swprintf_s(disabledPath, L"%s.disabled", cip);

        if (MoveFileExW(cip, disabledPath, MOVEFILE_REPLACE_EXISTING))
        {
            InternalLog(L"[+] {60FD87F8}.cip -> .disabled: Bir sonraki reboot'ta WDAC politikasi KALDIRILACAK!");
            InternalLog(L"[+] Simdi Samples.exe'yi kapatip bilgisayari yeniden baslatiniz.");
            return;
        }
        else
        {
            DWORD err = GetLastError();
            InternalLog(L"[!] {60FD87F8}.cip yeniden adlandirilamadi (hata=%lu). Silinmeye calisiliyor...", err);

            // Delete deneme
            if (DeleteFileW(cip))
            {
                InternalLog(L"[+] {60FD87F8}.cip SILINDI! Bir sonraki reboot'ta etkili.");
                return;
            }

            // MoveFileEx ile reboot'ta sil
            MoveFileExW(cip, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
            InternalLog(L"[!] Kilitlenemedi — reboot sirasinda silinmek uzere siraya alindi (2 reboot gerekebilir).");
            InternalLog(L"[!] Alternatif: KB5077181'i kaldirin veya WinRE'den elle silin.");
        }
    }
}

// -----------------------------------------------------------------------
// HVCI (Hypervisor-Protected Code Integrity) aktif mi kontrol et
// HVCI aktifse kernel driver yükleme BIOS seviyesinde engellenir.
// -----------------------------------------------------------------------
static bool IsHvciActive()
{
    DWORD val = 0, type = REG_DWORD, sz = sizeof(DWORD);
    HKEY hKey = nullptr;

    // Win11 path
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
        0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"Enabled", nullptr, &type, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
        if (val == 1) return true;
    }

    // Legacy path
    val = 0; sz = sizeof(DWORD);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
        0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExW(hKey, L"EnableVirtualizationBasedSecurity", nullptr, &type, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
        if (val == 1) return true;
    }

    return false;
}

static FnLogCallback g_logCb = nullptr;

void SetXignLogCallback(FnLogCallback cb)
{
    g_logCb = cb;
}

static void InternalLog(const wchar_t* fmt, ...)
{
    if (!g_logCb) return;
    wchar_t buf[2048] = {};
    va_list args;
    va_start(args, fmt);
    vswprintf_s(buf, fmt, args);
    va_end(args);
    g_logCb(L"%s", buf);
}

DWORD FindProcessByName(const wchar_t* procName)
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD foundPid = 0;

    if (Process32FirstW(hSnap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, procName) == 0)
            {
                foundPid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return foundPid;
}

bool WaitForModuleXign(Process& proc, const wchar_t* modName, DWORD timeoutMs)
{
    auto start = std::chrono::steady_clock::now();
    DWORD lastLoggedSec = 0;

    while (true)
    {
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        if (proc.modules().GetModule(modName))
        {
            auto mod = proc.modules().GetModule(modName);
            InternalLog(L"[+] '%s' modulu bellekte yakalandi! Taban Adresi: 0x%p, Boyut: 0x%X (%lld ms sonra)", 
                modName, (void*)mod->baseAddress, mod->size, elapsedMs);
            return true;
        }

        if (elapsedMs > (long long)timeoutMs)
        {
            InternalLog(L"[!] '%s' modulu icin zaman asimi doldu (%lu ms).", modName, timeoutMs);
            return false;
        }

        DWORD currentSec = static_cast<DWORD>(elapsedMs / 1000);
        if (currentSec != lastLoggedSec && currentSec > 0 && (currentSec % 2 == 0))
        {
            lastLoggedSec = currentSec;
            InternalLog(L"[*] '%s' bekleniyor... (Gecen sure: %lu sn / %lu sn)", modName, currentSec, timeoutMs / 1000);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

bool SuspendProcessXign(Process& proc)
{
    static auto NtSuspendProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtSuspendProcess");
    if (!NtSuspendProcess)
    {
        InternalLog(L"[-] NtSuspendProcess adresi ntdll.dll icinde bulunamadi!");
        return false;
    }

    NTSTATUS status = NtSuspendProcess(proc.core().handle());
    if (NT_SUCCESS(status))
    {
        InternalLog(L"[+] NtSuspendProcess basarili (STATUS: 0x%08X). Surec donduruldu.", status);
        return true;
    }
    else
    {
        InternalLog(L"[-] NtSuspendProcess basarisiz (STATUS: 0x%08X)!", status);
        return false;
    }
}

bool ResumeProcessXign(Process& proc)
{
    static auto NtResumeProcess = (NTSTATUS(NTAPI*)(HANDLE))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtResumeProcess");
    if (!NtResumeProcess)
    {
        InternalLog(L"[-] NtResumeProcess adresi ntdll.dll icinde bulunamadi!");
        return false;
    }

    NTSTATUS status = NtResumeProcess(proc.core().handle());
    if (NT_SUCCESS(status))
    {
        InternalLog(L"[+] NtResumeProcess basarili (STATUS: 0x%08X). Surec calismaya devam ediyor.", status);
        return true;
    }
    else
    {
        InternalLog(L"[-] NtResumeProcess basarisiz (STATUS: 0x%08X)!", status);
        return false;
    }
}

uintptr_t ManualMapDll(Process& proc, const wchar_t* dllPath)
{
    InternalLog(L"[*] ManualMap: Haritalama baslatiliyor (DLL: %s)...", dllPath);

    // Try loading the BlackBone kernel driver (HideVAD requires it).
    // Driver is x64-only; on a WOW64 process this may still succeed on x64 OS.
    eLoadFlags flags = static_cast<eLoadFlags>(
        static_cast<int>(eLoadFlags::WipeHeader) |
        static_cast<int>(eLoadFlags::ManualImports));

    {
        // --- KB5077181 WDAC platform politikasini devre disi birak ---
        // Bu politika testsigning+nointegritychecks'i bypass eder ve driver yuklemeyi bloklar.
        // Rename basarili olursa bir sonraki reboot'ta surucu yuklenebilir.
        TryDisableWdacPlatformPolicy();

        // --- SeLoadDriverPrivilege zorunlu, yoksa SCM driver yukleyemez ---
        if (EnableLoadDriverPrivilege())
            InternalLog(L"[+] SeLoadDriverPrivilege aktif edildi.");
        else
            InternalLog(L"[!] SeLoadDriverPrivilege aktif edilemedi — Admin olmadan driver yuklenemez.");

        // --- HVCI kontrolu ---
        bool hvciActive = IsHvciActive();
        if (hvciActive)
        {
            InternalLog(L"[!] HVCI (Hypervisor-Protected Code Integrity) AKTIF!");
            InternalLog(L"[!] HVCI aktifken imzasiz/blocklist'teki driver BIOS seviyesinde engellenir.");
            InternalLog(L"[!] Cozum: BIOS > Secure Boot KAPAT + Windows Security > Core Isolation > Memory Integrity KAPAT");
        }
        else
        {
            // HVCI kapali: WDAC blocklist'i devre disi birakabiliriz
            if (DisableWdacDriverBlocklist())
                InternalLog(L"[+] WDAC Vulnerable Driver Blocklist devre disi birakildi (registry yazildi).");
            else
                InternalLog(L"[!] WDAC blocklist registry yazma basarisiz — Admin hakki yeterli degil.");
        }

        // --- EXE klasoru ---
        wchar_t exePath[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        wchar_t* slash = wcsrchr(exePath, L'\\');
        std::wstring exeDir = slash ? std::wstring(exePath, slash + 1) : L"";

        // --- Aranacak tum dizinler ---
        // build\Win32\Debug  ->  ../../../  ->  proje koku
        // Proje koku altinda: src\BlackBoneDrv\bin\x64\Win10Release\ vb.
        auto buildParent = [&](int levels) -> std::wstring {
            std::wstring p = exeDir;
            for (int i = 0; i < levels; ++i)
            {
                // trailing backslash varsa kaldir
                if (!p.empty() && p.back() == L'\\') p.pop_back();
                auto pos = p.rfind(L'\\');
                if (pos == std::wstring::npos) return L"";
                p = p.substr(0, pos + 1);
            }
            return p;
        };

        std::wstring projRoot = buildParent(3); // build\Win32\Debug -> proje koku

        std::vector<std::wstring> searchDirs = {
            exeDir,                                                              // EXE'nin yanı
            projRoot,                                                            // Proje koku
            projRoot + L"src\\BlackBoneDrv\\bin\\x64\\Win10Release\\",
            projRoot + L"src\\BlackBoneDrv\\bin\\x64\\Win8.1Release\\",
            projRoot + L"src\\BlackBoneDrv\\bin\\x64\\Win8Release\\",
            projRoot + L"src\\BlackBoneDrv\\bin\\x64\\Win7Release\\",
            projRoot + L"build\\x64\\Debug\\",                                   // x64 Debug yanı
            projRoot + L"build\\x64\\Release\\",                                 // x64 Release yanı
        };

        // Current directory da ekle
        wchar_t curDir[MAX_PATH] = {};
        if (GetCurrentDirectoryW(MAX_PATH, curDir))
        {
            std::wstring cd = curDir;
            if (!cd.empty() && cd.back() != L'\\') cd += L'\\';
            searchDirs.push_back(cd);
        }

        // --- Driver arama isimleri ---
        // BlackBoneDrv10.sys : resmi isim (Win10/Win11)
        // BlackBone64.sys    : kullanici yeniden adlandirmis olabilir
        // BlackBoneWin10.sys : bazi fork'larda bu isim kullanilir
        // OS versiyonuna gore oncelik sirasi: Win10 > Win8.1 > Win8 > Win7
        const wchar_t* drvNames[] = {
            L"BlackBoneDrv10.sys",
            L"BlackBone64.sys",        // kullanici tarafindan yeniden adlandirilmis variant
            L"BlackBoneWin10.sys",     // fork variant
            L"BlackBoneDrv81.sys",
            L"BlackBoneDrv8.sys",
            L"BlackBoneDrv7.sys"
        };

        bool driverLoaded = false;
        for (auto* n : drvNames)
        {
            if (driverLoaded) break;
            for (const auto& dir : searchDirs)
            {
                if (dir.empty()) continue;
                std::wstring fullPath = dir + n;
                DWORD attr = GetFileAttributesW(fullPath.c_str());
                if (attr == INVALID_FILE_ATTRIBUTES) continue;

                InternalLog(L"[*] Surucu deneniyor: %s", fullPath.c_str());
                NTSTATUS st = Driver().EnsureLoaded(fullPath);
                if (NT_SUCCESS(st))
                {
                    flags = static_cast<eLoadFlags>(
                        static_cast<int>(flags) | static_cast<int>(eLoadFlags::HideVAD));
                    InternalLog(L"[+] BlackBone surucu yuklendi — HideVAD AKTIF. (%s)", fullPath.c_str());
                    driverLoaded = true;
                    break;
                }
                else
                {
                    DWORD scmWin32 = GetLastError(); // ScmErrToNt tarafindan SetLastError ile konuldu
                    const wchar_t* hint = L"";
                    if ((uint32_t)st == 0xC0000906u)
                        hint = L" [WDAC Blocklist — registry yazildi, YENIDEN BASLAT]";
                    else if ((uint32_t)st == 0xC0000428u)
                        hint = L" [Test Signing kapali — otomatik etkinlestiriliyor, REBOOT gerekiyor]";
                    else if ((uint32_t)st == 0xC0000365u)
                        hint = L" [Imza dogrulama hatasi — create_cert_eku_ADMIN.ps1 calistirin!]";
                    else if ((uint32_t)st == 0xC00000BBu)
                        hint = L" [WOW64 NtLoadDriver reddi]";
                    else if ((uint32_t)st == 0xC0000034u)
                        hint = L" [Dosya bulunamadi]";
                    else if ((uint32_t)st == 0xC0000022u)
                        hint = L" [Erisim reddedildi — Admin ile calistirin]";
                    InternalLog(L"[!] Surucu yuklenemedi: NTSTATUS=0x%08X Win32=%lu%s",
                                (uint32_t)st, scmWin32, hint);

                    // Test signing kapali ise otomatik etkinlestir
                    if ((uint32_t)st == 0xC0000428u)
                        TryEnableTestSigning();
                }
            }
        }

        if (!driverLoaded)
        {
            InternalLog(L"[!] BlackBone surucu yuklenemedi — HideVAD devre disi.");
            if (hvciActive)
            {
                InternalLog(L"[!] COZUM (HVCI aktif):");
                InternalLog(L"[!]   BIOS: Secure Boot KAPAT");
                InternalLog(L"[!]   Windows Security > Cihaz Guvenlik > Cekirdek izolasyonu > Bellek butunlugu KAPAT");
                InternalLog(L"[!]   Sonra YENIDEN BASLAT + Samples.exe tekrar calistir.");
            }
            else
            {
                InternalLog(L"[!] COZUM (WDAC blocklist):");
                InternalLog(L"[!]   Kayit defteri guncellendi (VulnerableDriverBlocklistEnable=0).");
                InternalLog(L"[!]   Windows'u YENIDEN BASLATIP Samples.exe tekrar calistirin.");
                InternalLog(L"[!]   Test signing acik olmali: bcdedit /set testsigning on");
            }
        }
    }

    auto image = proc.mmap().MapImage(dllPath, flags);

    if (!image)
    {
        InternalLog(L"[-] MapImage basarisiz! NTSTATUS: 0x%08X", image.status);
        return 0;
    }

    auto modInfo = image.result();
    uintptr_t base = static_cast<uintptr_t>(modInfo->baseAddress);
    InternalLog(L"[+] Manual Map tamamlandi. Base: 0x%p, Boyut: 0x%X", (void*)base, modInfo->size);

    return base;
}

bool RemoteUnlinkModule(Process& proc, uintptr_t modBase, const wchar_t* nameTag)
{
    if (!modBase)
    {
        InternalLog(L"[!] RemoteUnlinkModule: Gecersiz base address (0x0).");
        return false;
    }

    const DWORD PebLdrOffset = 0x0C; // x86 PEB+0x0C -> Ldr
    struct LdrCtx {
        DWORD LdrListOffset;
        DWORD EntryListOffset;
        const wchar_t* Name;
    };
    LdrCtx lists[] = {
        { 0x0C, 0x00, L"InLoadOrderModuleList" },
        { 0x14, 0x08, L"InMemoryOrderModuleList" },
        { 0x1C, 0x10, L"InInitializationOrderModuleList" }
    };

    PROCESS_BASIC_INFORMATION pbi = {};
    ULONG rl = 0;
    static auto NtQIP = (NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG))GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess");

    if (!NtQIP || NtQIP(proc.core().handle(), 0, &pbi, sizeof(pbi), &rl) != 0 || !pbi.PebBaseAddress)
    {
        InternalLog(L"[-] RemoteUnlinkModule: NtQueryInformationProcess basarisiz.");
        return false;
    }

    uintptr_t peb = (uintptr_t)pbi.PebBaseAddress;
    uintptr_t ldrPtr = 0;
    SIZE_T rd = 0;

    if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(peb + PebLdrOffset), &ldrPtr, sizeof(ldrPtr), &rd) || !ldrPtr)
    {
        InternalLog(L"[-] RemoteUnlinkModule: PEB_LDR_DATA okunamadi.");
        return false;
    }

    InternalLog(L"[*] Remote PEB taraniyor (PEB: 0x%p, Ldr: 0x%p, Hedef Base: 0x%p)...", 
        (void*)peb, (void*)ldrPtr, (void*)modBase);

    bool any = false;
    for (const auto& ctx : lists)
    {
        uintptr_t listHead = ldrPtr + ctx.LdrListOffset;
        uintptr_t current = 0;

        if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)listHead, &current, sizeof(current), &rd) || !current)
            continue;

        int guard = 0;
        while (current != listHead && current != 0 && guard++ < 512)
        {
            uintptr_t entryBase = current - ctx.EntryListOffset;

            uintptr_t readBase = 0;
            // 0x18 offset x86 LDR_DATA_TABLE_ENTRY -> DllBase
            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(entryBase + 0x18), &readBase, sizeof(readBase), &rd))
                break;

            if (readBase == modBase)
            {
                uintptr_t flink = 0, blink = 0;
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &flink, sizeof(flink), &rd);
                ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 4), &blink, sizeof(blink), &rd);

                SIZE_T bw = 0;
                WriteProcessMemory(proc.core().handle(), (LPVOID)(blink + 0), &flink, sizeof(flink), &bw);
                WriteProcessMemory(proc.core().handle(), (LPVOID)(flink + 4), &blink, sizeof(blink), &bw);

                DWORD zero[2] = { 0, 0 };
                WriteProcessMemory(proc.core().handle(), (LPVOID)current, zero, sizeof(zero), &bw);

                any = true;
                InternalLog(L"    [+] %s listesinden '%s' (0x%p) basariyla unlinked.", ctx.Name, nameTag, (void*)modBase);
                break;
            }

            if (!ReadProcessMemory(proc.core().handle(), (LPCVOID)(current + 0), &current, sizeof(current), &rd))
                break;
        }
    }
    return any;
}
