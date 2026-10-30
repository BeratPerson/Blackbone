#pragma once
#include <Windows.h>
#include <string>
#include "BlackBone/Process/Process.h"

// Loglama Callback türü
typedef void (*FnLogCallback)(const wchar_t* fmt, ...);

// Log callback ata
void SetXignLogCallback(FnLogCallback cb);

// Süreç arama
DWORD FindProcessByName(const wchar_t* procName);

// XignCode modülünü bekle
bool WaitForModuleXign(blackbone::Process& proc, const wchar_t* modName, DWORD timeoutMs);

// Süreç askıya alma / devam ettirme
bool SuspendProcessXign(blackbone::Process& proc);
bool ResumeProcessXign(blackbone::Process& proc);

// Manual Map ile DLL enjekte et
uintptr_t ManualMapDll(blackbone::Process& proc, const wchar_t* dllPath);

// Uzaktan PEB'den modül çıkar
bool RemoteUnlinkModule(blackbone::Process& proc, uintptr_t modBase, const wchar_t* nameTag);
