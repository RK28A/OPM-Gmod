#include "Memory.h"

#include <random>

void BytePatch(PVOID source, BYTE newValue)
{
    DWORD originalProtection;
    VirtualProtect(source, sizeof(PVOID), PAGE_EXECUTE_READWRITE, &originalProtection);
    *(BYTE*)(source) = newValue;
    VirtualProtect(source, sizeof(PVOID), originalProtection, &originalProtection);
}

std::vector<hookData> vmtHooks;

// `original` rather than `dst`: this writes the *saved* function back, and the
// parameter carrying it was named as though it were the detour.
void RestoreVMTHook(PVOID** src, PVOID original, int index)
{
    if (!src || !*src || index < 0)
        return;

    PVOID* VMT = *src;
    DWORD originalProtection;
    if (!VirtualProtect(&VMT[index], sizeof(PVOID), PAGE_EXECUTE_READWRITE, &originalProtection))
        return;

    VMT[index] = original;
    VirtualProtect(&VMT[index], sizeof(PVOID), originalProtection, &originalProtection);
}

void RestoreVMTHooks()
{
    // Reverse order, so a vtable entry hooked twice ends up holding the value it
    // had before the first hook rather than before the last.
    for (std::size_t i = vmtHooks.size(); i-- > 0; )
        RestoreVMTHook(vmtHooks[i].src, vmtHooks[i].original, vmtHooks[i].index);

    // Cleared, so a second unload is a no-op instead of replaying every restore
    // against vtables that may since have been replaced.
    vmtHooks.clear();
}

std::vector<std::string> missingPatterns;

 // credits to osiris for the following
static auto generateBadCharTable(std::string_view pattern) noexcept
{
    std::array<std::size_t, 256> table;

    auto lastWildcard = pattern.rfind('?');
    if (lastWildcard == std::string_view::npos)
        lastWildcard = 0;

    const auto defaultShift = (std::max)(std::size_t(1), pattern.length() - 1 - lastWildcard);
    table.fill(defaultShift);

    for (auto i = lastWildcard; i < pattern.length() - 1; ++i)
        table[static_cast<std::uint8_t>(pattern[i])] = pattern.length() - 1 - i;

    return table;
}

// Returns nullptr when the scan fails, and records the name so the caller can
// report every failure at once.
//
// It used to pop a MessageBoxA -- from whatever thread happened to be running,
// which during start-up is a detached thread inside DllMain's shadow -- and then
// return 0 into arithmetic that dereferences it (see GetRealFromRelative).
const char* findPattern(const char* moduleName, std::string_view pattern, std::string_view patternName) noexcept
{
    if (pattern.empty())
        return nullptr;

    PVOID moduleBase = 0;
    std::size_t moduleSize = 0;
    if (HMODULE handle = GetModuleHandleA(moduleName))
        if (MODULEINFO moduleInfo; GetModuleInformation(GetCurrentProcess(), handle, &moduleInfo, sizeof(moduleInfo)))
        {
            moduleBase = moduleInfo.lpBaseOfDll;
            moduleSize = moduleInfo.SizeOfImage;
        }

    if (moduleBase && moduleSize >= pattern.length()) {
        const std::size_t lastIdx = pattern.length() - 1;
        const auto badCharTable = generateBadCharTable(pattern);

        auto start = static_cast<const char*>(moduleBase);
        const auto end = start + moduleSize - pattern.length();

        while (start <= end) {
            std::size_t i = lastIdx;
            for (;;)
            {
                if (pattern[i] != '?' && start[i] != pattern[i])
                    break;

                if (i == 0)
                    return start;

                --i;
            }

            start += badCharTable[static_cast<std::uint8_t>(start[lastIdx])];
        }
    }

    try
    {
        missingPatterns.emplace_back(patternName);
    }
    catch (...)
    {
        // Reporting a failed scan must not itself throw out of a noexcept
        // function.
    }

    return nullptr;
}

// Address must be an instruction, not a pointer!  And offset = the offset to the
// bytes you want to retrieve.
//
// Returns nullptr for a null address instead of reading through it: the callers
// feed this straight from findPattern(), whose failure value is null, and the
// null checks that were added downstream all sat *after* this dereference.
char* GetRealFromRelative(char* address, int offset, int instructionSize, bool isRelative)
{
    if (!address)
        return nullptr;

#ifdef _WIN64
    isRelative = true;
#endif
    // A failed pattern scan hands us a null `address`; dereferencing address+offset
    // would then fault. Bail to null so the caller sees a clean failure (it is
    // logged in Main) instead of the game crashing during resolution.
    if (!address)
        return nullptr;
    char* instruction = address + offset;
    if (!isRelative)
    {
        if (!MemIsReadable(instruction, sizeof(char*)))
            return nullptr;
        return *(char**)(instruction);
    }

    if (!MemIsReadable(instruction, sizeof(int)))
        return nullptr;
    int relativeAddress = *(int*)(instruction);
    char* realAddress = address + instructionSize + relativeAddress;
    return realAddress;
}

// rand() % (length - 1) never produced the last character of the alphabet --
// '9' was unreachable -- and rand() shares global state with anything else in
// the process that seeds it.
std::string RandomString(int length)
{
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    if (length <= 0)
        return std::string();

    static std::mt19937 engine{ std::random_device{}() };
    std::uniform_int_distribution<std::size_t> distribution(0, alphabet.size() - 1);

    std::string output;
    output.reserve(static_cast<std::size_t>(length));

    for (int i = 0; i < length; i++)
        output += alphabet[distribution(engine)];

    return output;
}
