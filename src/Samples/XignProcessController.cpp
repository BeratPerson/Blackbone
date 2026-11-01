#include "XignProcessController.hpp"
#include "ManualMapXign.h"
#include "BlackBone/Process/Process.h"
#include "BlackBone/ManualMap/MMap.h"
#include "BlackBone/DriverControl/DriverControl.h"
#include "kernel/XignDriverInterface.hpp"
#include <windows.h>
#include <cstdarg>
#include <thread>
#include <chrono>

using namespace blackbone;

namespace XignSamples
{
    XignProcessController::XignProcessController(std::shared_ptr<Xign::IDriverClient> driverClient)
        : m_driverClient(std::move(driverClient))
    {
        if (!m_driverClient)
        {
            m_driverClient = std::make_shared<Xign::Xhunter2Driver>();
        }
    }

    XignProcessController::~XignProcessController()
    {
        ShutdownDriver();
    }

    void XignProcessController::SetLogCallback(LogCallback callback)
    {
        m_logCallback = std::move(callback);
    }

    void XignProcessController::SetDriverClient(std::shared_ptr<Xign::IDriverClient> driverClient)
    {
        m_driverClient = std::move(driverClient);
    }

    std::shared_ptr<Xign::IDriverClient> XignProcessController::GetDriverClient() const
    {
        return m_driverClient;
    }

    void XignProcessController::Log(const wchar_t* fmt, ...)
    {
        if (!m_logCallback) return;
        wchar_t buf[2048] = {};
        va_list args;
        va_start(args, fmt);
        _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, args);
        va_end(args);
        m_logCallback(buf);
    }

    bool XignProcessController::InitializeDriver(const std::vector<uint8_t>& wbmfBytes, const std::wstring& deviceName)
    {
        if (!m_driverClient) return false;
        Log(L"[*] Initializing driver interface...");
        bool ok = m_driverClient->Open(wbmfBytes, deviceName);
        if (ok)
        {
            Log(L"[+] Driver interface initialized successfully.");
        }
        else
        {
            Log(L"[-] Driver initialization failed or bypassed by environment.");
        }
        return ok;
    }

    void XignProcessController::ShutdownDriver()
    {
        if (m_driverClient && m_driverClient->IsReady())
        {
            Log(L"[*] Shutting down driver interface...");
            m_driverClient->Close();
            Log(L"[+] Driver interface closed.");
        }
    }

    bool XignProcessController::IsDriverReady() const
    {
        return m_driverClient && m_driverClient->IsReady();
    }

    // -----------------------------------------------------------------------
    // Helper — process'in 32/64 bit oldugunu kontrol et
    // -----------------------------------------------------------------------
    static bool IsProcess32Bit(HANDLE hProc)
    {
        BOOL wow = FALSE;
        if (IsWow64Process(hProc, &wow)) return wow;
        return false; // x64 OS'ta IsWow64Process basarisizsa 64-bit varsay
    }

    InjectionResult XignProcessController::ExecuteInjection(uint32_t pid, const std::wstring& dllPath)
    {
        InjectionResult result;
        result.FailedStep = InjectionStep::ValidatingInputs;

        Log(L"[===] ExecuteInjection BASLADI PID=%lu", pid);

        // Step 1: Validate file path
        std::wstring absDllPath = dllPath;
        wchar_t pathBuf[MAX_PATH] = {};
        DWORD pathLen = GetFullPathNameW(dllPath.c_str(), MAX_PATH, pathBuf, nullptr);
        if (pathLen && pathLen < MAX_PATH)
        {
            absDllPath = pathBuf;
        }
        Log(L"[*] DLL tam yolu: %s", absDllPath.c_str());

        DWORD attr = GetFileAttributesW(absDllPath.c_str());
        if (attr == INVALID_FILE_ATTRIBUTES)
        {
            result.LastWin32Error = GetLastError();
            result.StatusMessage = L"DLL dosyasi bulunamadi: " + absDllPath;
            Log(L"[HATA] %s  (Win32=%lu)", result.StatusMessage.c_str(), result.LastWin32Error);
            Log(L"[IPUCU] berat.dll'i Samples.exe ile ayni klasore koyun.");
            return result;
        }

        // Architecture check — 32/64 uyumu
        HANDLE hTmpProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hTmpProc)
        {
            bool target32 = IsProcess32Bit(hTmpProc);
            CloseHandle(hTmpProc);
#ifdef _WIN64
            bool injector64 = true;
#else
            bool injector64 = false;
#endif
            bool dll32 = false; // Dosyadan PE header okuyarak kontrol et
            {
                HANDLE hFile = CreateFileW(absDllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (hFile != INVALID_HANDLE_VALUE)
                {
                    IMAGE_DOS_HEADER dos = {};
                    DWORD rd = 0;
                    ReadFile(hFile, &dos, sizeof(dos), &rd, nullptr);
                    if (dos.e_magic == IMAGE_DOS_SIGNATURE)
                    {
                        SetFilePointer(hFile, dos.e_lfanew + 4, nullptr, FILE_BEGIN);
                        WORD machine = 0;
                        ReadFile(hFile, &machine, sizeof(machine), &rd, nullptr);
                        dll32 = (machine == IMAGE_FILE_MACHINE_I386);
                    }
                    CloseHandle(hFile);
                }
            }
            Log(L"[*] Mimari kontrol: Injector=%s | Hedef Surec=%s | DLL=%s",
                injector64 ? L"x64" : L"x86",
                target32   ? L"x86(WOW64)" : L"x64",
                dll32      ? L"x86" : L"x64");

            if (injector64 && target32)
            {
                Log(L"[UYARI] x64 injector -> x86 hedef! 32-bit Samples.exe kullanin.");
                result.StatusMessage = L"Mimari uyumsuzlugu: x64 injector ile x86 process inject edilemez. 32-bit Samples.exe kullanin.";
                Log(L"[HATA] %s", result.StatusMessage.c_str());
                result.LastWin32Error = ERROR_NOT_SUPPORTED;
                return result;
            }
            if (!dll32 && target32)
            {
                Log(L"[UYARI] x64 DLL x86 procese inject edilemiyor! 32-bit berat.dll kullanin.");
            }
        }

        Log(L"[*] Enjeksiyon siralama basladi PID: %lu", pid);
        Log(L"[*] Hedef DLL: %s", absDllPath.c_str());

        // Step 2: Driver registration if active
        if (IsDriverReady())
        {
            Log(L"[*] Registering PID with driver gate...");
            if (m_driverClient->RegisterProcess(pid))
            {
                Log(L"[+] Driver PID registration succeeded.");
            }
            else
            {
                Log(L"[!] Driver PID registration returned non-success; proceeding with standard pipeline.");
            }
        }

        // Step 3: Attach to process
        result.FailedStep = InjectionStep::AttachingProcess;
        Log(L"[*] Surece baglaniliyor PID=%lu ...", pid);
        Process proc;
        NTSTATUS attachStatus = proc.Attach(pid);
        if (!NT_SUCCESS(attachStatus))
        {
            result.LastNtStatus = attachStatus;
            result.LastWin32Error = GetLastError();
            result.StatusMessage = L"Surece baglanilamadi.";
            Log(L"[HATA] %s  NTSTATUS=0x%08X  Win32=%lu", result.StatusMessage.c_str(),
                (uint32_t)attachStatus, result.LastWin32Error);
            Log(L"[IPUCU] Surec calismiyor ya da yetki yetersiz. Admin olarak calistirin.");
            return result;
        }
        Log(L"[+] Surece baglandi. Handle: 0x%p", proc.core().handle());

        // Step 4: Module timing check
        result.FailedStep = InjectionStep::WaitingAntiCheatModule;
        Log(L"[*] Checking for x3.xem module...");
        bool x3Found = WaitForModuleXign(proc, L"x3.xem", 5000);
        if (x3Found)
        {
            auto x3mod = proc.modules().GetModule(L"x3.xem");
            if (x3mod)
            {
                Log(L"[+] Found x3.xem @ 0x%p (size: 0x%X)", (void*)x3mod->baseAddress, x3mod->size);
            }
        }

        // Step 5: Perform Manual Map
        // NOT: xhunter2 init tamamlandiktan sonra inject yapiyoruz (InjectorUI watcher garantisi).
        // x3.xem'i PEB unlink icin onceden suspend etmiyoruz — o adim init'i bozuyordu.
        result.FailedStep = InjectionStep::MappingImage;
        Log(L"[*] BlackBone ManualMap baslatiliyor...");
        Log(L"[*] WipeHeader modu aktif (HideVAD driver gerektiriyor).");
        uintptr_t mappedBase = ManualMapDll(proc, absDllPath.c_str());
        if (!mappedBase)
        {
            result.LastWin32Error = GetLastError();
            result.StatusMessage = L"ManualMapDll basarisiz: DLL haritalama hatasi.";
            Log(L"[HATA] %s", result.StatusMessage.c_str());
            Log(L"[HATA] Olasiliklar: Yetki yok (admin?), DLL bozuk, mimari uyumsuzlugu, AV mudahalesi.");
            Log(L"[IPUCU] Samples.exe'yi yonetici olarak calistirin.");
            proc.Detach();
            return result;
        }

        result.MappedBase = mappedBase;
        Log(L"[+] Image mapped successfully at: 0x%p", (void*)mappedBase);

        // Step 7: Post-mapping cleanup
        // DllMain calistiktan sonra PEB unlink yapiyoruz.
        // x3.xem init tamamlansin diye 800ms bekle — cok erken suspend edilirse
        // xhunter init'i yari keser ve oyun kapanir.
        result.FailedStep = InjectionStep::PostMappingCleanup;
        Log(L"[*] DllMain stabilizasyonu icin 800 ms bekleniyor...");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        SuspendProcessXign(proc);
        // ManualMap ile inject edilen DLL zaten PEB LDR listesine YAZILMAZ —
        // bu BlackBone'un tasarimi geregi, normal LoadLibrary ile fark bu.
        // RemoteUnlinkModule PEB'de bulamazsa bu HATALI degil, DOGRU davranis.
        bool unlinkOk = RemoteUnlinkModule(proc, mappedBase, L"injected.dll");
        if (unlinkOk)
            Log(L"[+] PEB unlink: giris bulunup kaldirildi (eski inject izleri temizlendi).");
        else
            Log(L"[+] PEB unlink: ManualMap ile inject edildigi icin PEB LDR'da zaten kayitli degil (stealth OK).");
        ResumeProcessXign(proc);

        // Step 8: Detach
        result.FailedStep = InjectionStep::DetachingProcess;
        proc.Detach();
        Log(L"[*] Process detached safely.");

        result.Success = true;
        result.FailedStep = InjectionStep::Completed;
        result.StatusMessage = L"Injection completed successfully.";
        Log(L"[+] %s", result.StatusMessage.c_str());

        return result;
    }
}
