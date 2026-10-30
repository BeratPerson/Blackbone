#pragma once
#include <Windows.h>
#include <vector>
#include "BlackBone/Process/Process.h"

class GameMemory
{
    blackbone::Process* _proc;

public:
    GameMemory(blackbone::Process& proc) : _proc(&proc) {}

    template<typename T>
    bool Read(uintptr_t addr, T& out) {
        auto status = _proc->memory().Read(addr, sizeof(T), &out);
        return NT_SUCCESS(status);
    }

    template<typename T>
    bool Write(uintptr_t addr, const T& in) {
        auto status = _proc->memory().Write(addr, sizeof(T), &in);
        return NT_SUCCESS(status);
    }

    // String oku (Knight Online isimleri gibi)
    bool ReadString(uintptr_t addr, char* buf, size_t maxLen);

    // Pointer chain: base -> [off1] -> [off2] -> ... -> final
    uintptr_t ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets);
};