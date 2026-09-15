#pragma once

#include <Windows.h>

#include <string_view>
#include <psapi.h>
#include <signal.h>
#include <limits>
#include <array>
#include <string>
#include <vector>

void BytePatch(PVOID source, BYTE newValue);

struct hookData {
    PVOID** src;
    PVOID dst;
    int index;
};
extern std::vector<hookData> vmtHooks;

// True only if every byte of [ptr, ptr+size) lives in a committed, readable
// page. This is what makes the offset/scan-derived pointer resolution below
// non-fatal: a hard-coded byte offset into a game function (ViewRenderOffset
// and friends) is the first thing to rot after a Garry's Mod update, and a
// stale one resolves to a wild pointer. Checking readability first lets the
// resolver bail out (returning null) and the hook installer skip it, instead
// of the whole game crashing on the first dereference.
inline bool MemIsReadable(const void* ptr, size_t size = sizeof(void*)) noexcept
{
    if (!ptr || size == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t cur = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t end = cur + size;

    while (cur < end)
    {
        if (VirtualQuery(reinterpret_cast<LPCVOID>(cur), &mbi, sizeof(mbi)) != sizeof(mbi))
            return false;
        if (mbi.State != MEM_COMMIT)
            return false;

        const DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
            | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & readable) || (mbi.Protect & PAGE_GUARD))
            return false;

        cur = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

template<typename T>
T VMTHook(PVOID** src, PVOID dst, int index, bool noRestore = false)
{
    // I could do tramp hooking instead of VMT hooking, but I came across a few problems while implementing my tramp, and VMT just makes it easier.
    // Refuse to hook through an object whose vtable slot is not actually
    // mapped: `src` can be a stale/garbage interface pointer after a game
    // update, and *src / VMT[index] would then fault. Bail to null so the
    // caller (and the crash-free init path) can carry on in a degraded state.
    if (!MemIsReadable(src) || !MemIsReadable(*src, (static_cast<size_t>(index) + 1) * sizeof(PVOID)))
        return (T)nullptr;

    PVOID* VMT = *src;
    PVOID ret = (VMT[index]);
    DWORD originalProtection;
    VirtualProtect(&VMT[index], sizeof(PVOID), PAGE_EXECUTE_READWRITE, &originalProtection);
    VMT[index] = dst;
    VirtualProtect(&VMT[index], sizeof(PVOID), originalProtection, &originalProtection);
    if (!noRestore)
    {
        hookData currData = { src, ret, index };
        vmtHooks.push_back(currData);
    }
    return (T)ret;
};
void RestoreVMTHook(PVOID** src, PVOID dst, int index);
void RestoreVMTHooks();

const char* findPattern(const char* moduleName, std::string_view pattern, std::string patternName) noexcept;

char* GetRealFromRelative(char* address, int offset, int instructionSize = 6, bool isRelative = true); // Address must be a CALL instruction, not a pointer! And offset the offset to the bytes you want to retrieve.

template<typename T>
T* GetVMT(uintptr_t address, int index, uintptr_t offset) // Address must be a VTable pointer, not a VTable !
{
    // Every dereference here walks memory whose location comes from a
    // hard-coded offset. Each step is validated so a stale offset returns null
    // rather than crashing (see MemIsReadable above).
    if (!MemIsReadable(reinterpret_cast<void*>(address)))
        return nullptr;
    uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(address);
    if (!MemIsReadable(vtable, (static_cast<size_t>(index) + 1) * sizeof(uintptr_t)))
        return nullptr;
#ifdef _WIN64
    const uintptr_t step = 3;
    const uintptr_t instructionSize = 7;
    uintptr_t instruction = (vtable[index] + offset);
    if (!MemIsReadable(reinterpret_cast<void*>(instruction + step), sizeof(DWORD)))
        return nullptr;

    uintptr_t relativeAddress = *(DWORD*)(instruction + step);
    uintptr_t realAddress = instruction + instructionSize + relativeAddress;
    if (!MemIsReadable(reinterpret_cast<void*>(realAddress)))
        return nullptr;
    return *(T**)(realAddress);
#else
    uintptr_t instruction = (vtable[index] + offset);
    if (!MemIsReadable(reinterpret_cast<void*>(instruction), sizeof(uintptr_t)))
        return nullptr;
    uintptr_t realAddress = *(uintptr_t*)(instruction);
    if (!MemIsReadable(reinterpret_cast<void*>(realAddress)))
        return nullptr;
    return *(T**)(realAddress);
#endif
}
template<typename T>
T* GetVMT(uintptr_t address, uintptr_t offset) // This doesn't reads from the VMT, address must be the function's base ! Not a pointer!
{
#ifdef _WIN64
    const uintptr_t step = 3;
    const uintptr_t instructionSize = 7;
    uintptr_t instruction = address + offset;
    if (!MemIsReadable(reinterpret_cast<void*>(instruction + step), sizeof(DWORD)))
        return nullptr;

    uintptr_t relativeAddress = *(DWORD*)(instruction + step);
    uintptr_t realAddress = instruction + instructionSize + relativeAddress;
    if (!MemIsReadable(reinterpret_cast<void*>(realAddress)))
        return nullptr;
    return *(T**)(realAddress);
#else
    uintptr_t instruction = (address + offset);
    if (!MemIsReadable(reinterpret_cast<void*>(instruction), sizeof(uintptr_t)))
        return nullptr;
    uintptr_t realAddress = *(uintptr_t*)(instruction);
    if (!MemIsReadable(reinterpret_cast<void*>(realAddress)))
        return nullptr;
    return *(T**)(realAddress);
#endif
}

std::string RandomString(int length);