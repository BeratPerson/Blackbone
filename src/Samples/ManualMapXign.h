#pragma once
#include <Windows.h>
#include "BlackBone/Process/Process.h"

// XignCode modulunu bekle
bool WaitForModuleXign(blackbone::Process& proc, const wchar_t* modName, DWORD timeoutMs);

// Surec askiya alma / devam ettirme
bool SuspendProcessXign(blackbone::Process& proc);
bool ResumeProcessXign(blackbone::Process& proc);

// Manual Map ile DLL enjekte et. Donus: mapped image base (basarisizsa 0)
uintptr_t ManualMapDll(blackbone::Process& proc, const wchar_t* dllPath);

// Uzaktan PEB'den modul cikar (x3.xem veya berat.dll icin)
bool RemoteUnlinkModule(blackbone::Process& proc, uintptr_t modBase, const wchar_t* nameTag);
