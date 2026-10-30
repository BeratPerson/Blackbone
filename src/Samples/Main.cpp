#include <Windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "BlackBone/Process/Process.h"
#include "ManualMapXign.h"
#include "GameMemory.h"

#pragma comment(lib, "Blackbone.lib")

using namespace blackbone;

int wmain(int argc, wchar_t* argv[])
{
    if (argc < 2) {
        std::wcout << L"Kullanim: Injector.exe <PID>\n";
        return 1;
    }

    DWORD pid = std::wcstoul(argv[1], nullptr, 10);
    std::wcout << L"[+] Hedef PID: " << pid << std::endl;

    // 1. Process'e attach ol
    Process proc;
    if (!NT_SUCCESS(proc.Attach(pid))) {
        std::wcerr << L"[-] Process'e attach olunamadi." << std::endl;
        return 1;
    }
    std::wcout << L"[+] Attach basarili." << std::endl;

    // 2. x3.xem yuklenene kadar bekle (max 25sn)
    std::wcout << L"[*] x3.xem bekleniyor (max 25sn)..." << std::endl;
    if (!WaitForModuleXign(proc, L"x3.xem", 25000)) {
        std::wcout << L"[!] x3.xem zaman asimina ugradi, devam ediliyor..." << std::endl;
    }
    else {
        std::wcout << L"[+] x3.xem bellekte." << std::endl;
    }

    // 3. SURECI DONDUR (Atomic bypass baslangici)
    std::wcout << L"[*] Surec askiya aliniyor..." << std::endl;
    if (!SuspendProcessXign(proc)) {
        std::wcerr << L"[-] Suspend basarisiz!" << std::endl;
        return 1;
    }

    // 4. x3.xem PEB'den cikar (Remote Unlink)
    auto x3mod = proc.modules().GetModule(L"x3.xem");
    if (x3mod) {
        std::wcout << L"[+] x3.xem Base: 0x" << std::hex << x3mod->baseAddress << std::dec << std::endl;
        RemoteUnlinkModule(proc, static_cast<uintptr_t>(x3mod->baseAddress), L"x3.xem");
    }
    else {
        std::wcout << L"[!] x3.xem base bulunamadi, unlink atlaniyor." << std::endl;
    }

    // 5. berat.dll MANUAL MAP ile enjekte et (LoadLibrary cagrilmaz!)
    std::wcout << L"[*] Manual Map inject baslatiliyor..." << std::endl;
    uintptr_t mappedBase = ManualMapDll(proc, L"berat.dll");
    if (!mappedBase) {
        std::wcerr << L"[-] Inject basarisiz! Surec devam ettirilecek." << std::endl;
        ResumeProcessXign(proc);
        return 1;
    }

    // 6. berat.dll PEB'den gizle (Self-hide remote)
    std::wcout << L"[*] berat.dll PEB unlink yapiliyor..." << std::endl;
    RemoteUnlinkModule(proc, mappedBase, L"berat.dll");

    // 7. SURECI DEVAM ETTIR
    std::wcout << L"[*] Surec devam ettiriliyor..." << std::endl;
    ResumeProcessXign(proc);

    // 8. (Opsiyonel) Oyun belleginden okuma/yazma testi
    GameMemory mem(proc);
    uintptr_t ptrTest = 0;
    if (mem.Read<uintptr_t>(mappedBase, ptrTest)) {
        std::wcout << L"[+] Bellek okuma aktif. Ornek deger: 0x" << std::hex << ptrTest << std::endl;
    }

    std::wcout << L"[+] Bypass zinciri tamamlandi." << std::endl;
    proc.Detach();
    return 0;
}