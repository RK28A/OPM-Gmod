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
    PVOID original; // the function that was there before the hook, not the detour
    int index;
};
extern std::vector<hookData> vmtHooks;

template<typename T>
T VMTHook(PVOID** src, PVOID dst, int index, bool noRestore = false)
{
    // I could do tramp hooking instead of VMT hooking, but I came across a few problems while implementing my tramp, and VMT just makes it easier.
    //
    // src is the object whose vtable pointer is being rewritten.  Every caller
    // passes an interface resolved at start-up, any of which can be null when
    // the resolution failed -- this used to dereference it regardless.
    if (!src || !*src || index < 0)
        return (T)nullptr;

    PVOID* VMT = *src;
    PVOID ret = (VMT[index]);
    DWORD originalProtection;
    if (!VirtualProtect(&VMT[index], sizeof(PVOID), PAGE_EXECUTE_READWRITE, &originalProtection))
        return (T)nullptr;

    VMT[index] = dst;
    VirtualProtect(&VMT[index], sizeof(PVOID), originalProtection, &originalProtection);
    if (!noRestore)
    {
        hookData currData = { src, ret, index };
        vmtHooks.push_back(currData);
    }
    return (T)ret;
};
// `original` is the function to write back, not the detour -- the parameter was
// named `dst` as though it were the latter.
void RestoreVMTHook(PVOID** src, PVOID original, int index);

// Restores every hook recorded by VMTHook() and clears the record, so a second
// call is a no-op rather than a replay.
void RestoreVMTHooks();

// Names of the signature scans that failed, in the order they were attempted.
// Main() reports them together and refuses to install anything if it is not
// empty: a failed scan used to pop a MessageBox from a detached start-up thread
// and then return null into pointer arithmetic that dereferences it.
extern std::vector<std::string> missingPatterns;

const char* findPattern(const char* moduleName, std::string_view pattern, std::string_view patternName) noexcept;

// Address must be a CALL instruction, not a pointer! And offset the offset to
// the bytes you want to retrieve.  Returns nullptr for a null address rather
// than reading through it.
char* GetRealFromRelative(char* address, int offset, int instructionSize = 6, bool isRelative = true);

template<typename T>
T* GetVMT(uintptr_t address, int index, uintptr_t offset) // Address must be a VTable pointer, not a VTable !
{
    if (!address || index < 0)
        return nullptr;

#ifdef _WIN64
    uintptr_t step = 3;
    uintptr_t instructionSize = 7;
    uintptr_t instruction = ((*(uintptr_t**)(address))[index] + offset);

    uintptr_t relativeAddress = *(DWORD*)(instruction + step);
    uintptr_t realAddress = instruction + instructionSize + relativeAddress;
    return *(T**)(realAddress);
#else
    uintptr_t instruction = ((*(uintptr_t**)(address))[index] + offset);
    return *(T**)(*(uintptr_t*)(instruction));
#endif
}
template<typename T>
T* GetVMT(uintptr_t address, uintptr_t offset) // This doesn't reads from the VMT, address must be the function's base ! Not a pointer!
{
    if (!address)
        return nullptr;

#ifdef _WIN64
    uintptr_t step = 3;
    uintptr_t instructionSize = 7;
    uintptr_t instruction = address + offset;

    uintptr_t relativeAddress = *(DWORD*)(instruction + step);
    uintptr_t realAddress = instruction + instructionSize + relativeAddress;
    return *(T**)(realAddress);
#else
    uintptr_t instruction = (address + offset);
    return *(T**)(*(uintptr_t*)(instruction));
#endif
}

std::string RandomString(int length);