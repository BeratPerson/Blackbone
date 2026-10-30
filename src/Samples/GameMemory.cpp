#include "GameMemory.h"

bool GameMemory::ReadString(uintptr_t addr, char* buf, size_t maxLen)
{
    if (!addr || !buf || !maxLen) return false;
    auto st = _proc->memory().Read(addr, maxLen, buf);
    if (NT_SUCCESS(st)) {
        buf[maxLen - 1] = '\0';
        return true;
    }
    return false;
}

uintptr_t GameMemory::ReadPointerChain(uintptr_t base, const std::vector<uintptr_t>& offsets)
{
    uintptr_t ptr = base;
    for (size_t i = 0; i < offsets.size(); ++i) {
        if (!Read<uintptr_t>(ptr + offsets[i], ptr))
            return 0;
        if (!ptr) return 0;
    }
    return ptr;
}