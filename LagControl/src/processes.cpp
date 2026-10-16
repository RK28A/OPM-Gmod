#include "processes.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <map>
#include <unordered_map>

std::wstring ToLower(std::wstring s)
{
    if (!s.empty())
        CharLowerBuffW(s.data(), static_cast<DWORD>(s.size()));
    return s;
}

std::string ToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

namespace {

// Appelle une fonction Get*Table en agrandissant le tampon si nécessaire.
template <class Fn>
std::vector<unsigned char> FetchTable(Fn fn)
{
    std::vector<unsigned char> buf(64 * 1024);
    for (int attempt = 0; attempt < 6; ++attempt) {
        DWORD size = static_cast<DWORD>(buf.size());
        DWORD r = fn(buf.data(), &size);
        if (r == NO_ERROR) return buf;
        if (r != ERROR_INSUFFICIENT_BUFFER) break;
        buf.resize(size + 16 * 1024);
    }
    return {};
}

// Appelle cb(pid, isTcp, portLocal) pour chaque socket TCP/UDP, IPv4 et IPv6.
template <class Cb>
void ForEachSocket(Cb cb)
{
    auto tcp4 = FetchTable([](void* p, DWORD* s) {
        return GetExtendedTcpTable(p, s, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (!tcp4.empty()) {
        auto* t = reinterpret_cast<MIB_TCPTABLE_OWNER_PID*>(tcp4.data());
        for (DWORD i = 0; i < t->dwNumEntries; ++i)
            cb(t->table[i].dwOwningPid, true, ntohs(static_cast<u_short>(t->table[i].dwLocalPort)));
    }
    auto tcp6 = FetchTable([](void* p, DWORD* s) {
        return GetExtendedTcpTable(p, s, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    });
    if (!tcp6.empty()) {
        auto* t = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID*>(tcp6.data());
        for (DWORD i = 0; i < t->dwNumEntries; ++i)
            cb(t->table[i].dwOwningPid, true, ntohs(static_cast<u_short>(t->table[i].dwLocalPort)));
    }
    auto udp4 = FetchTable([](void* p, DWORD* s) {
        return GetExtendedUdpTable(p, s, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    });
    if (!udp4.empty()) {
        auto* t = reinterpret_cast<MIB_UDPTABLE_OWNER_PID*>(udp4.data());
        for (DWORD i = 0; i < t->dwNumEntries; ++i)
            cb(t->table[i].dwOwningPid, false, ntohs(static_cast<u_short>(t->table[i].dwLocalPort)));
    }
    auto udp6 = FetchTable([](void* p, DWORD* s) {
        return GetExtendedUdpTable(p, s, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    });
    if (!udp6.empty()) {
        auto* t = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID*>(udp6.data());
        for (DWORD i = 0; i < t->dwNumEntries; ++i)
            cb(t->table[i].dwOwningPid, false, ntohs(static_cast<u_short>(t->table[i].dwLocalPort)));
    }
}

template <class Cb>
void ForEachProcess(Cb cb)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            cb(pe.th32ProcessID, pe.szExeFile);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool IsHiddenProcess(DWORD pid, const std::wstring& key)
{
    static const wchar_t* kHidden[] = {L"[system process]", L"system", L"registry", L"memory compression",
                                       L"secure system", L"system idle process"};
    if (pid == 0 || pid == 4 || pid == GetCurrentProcessId()) return true;
    for (auto* h : kHidden)
        if (key == h) return true;
    return false;
}

}  // namespace

std::vector<AppInfo> ListApps()
{
    std::unordered_map<DWORD, int> connections;
    ForEachSocket([&](DWORD pid, bool, uint16_t) { ++connections[pid]; });

    std::map<std::wstring, AppInfo> apps;
    ForEachProcess([&](DWORD pid, const wchar_t* exe) {
        std::wstring name = exe;
        std::wstring key = ToLower(name);
        if (name.empty() || IsHiddenProcess(pid, key)) return;
        AppInfo& app = apps[key];
        if (app.name.empty()) {
            app.name = name;
            app.key = key;
            app.utf8 = ToUtf8(name);
        }
        app.pids.push_back(pid);
        auto it = connections.find(pid);
        if (it != connections.end()) app.connections += it->second;
    });

    std::vector<AppInfo> out;
    out.reserve(apps.size());
    for (auto& kv : apps) out.push_back(std::move(kv.second));
    std::stable_sort(out.begin(), out.end(), [](const AppInfo& a, const AppInfo& b) {
        return (a.connections > 0) > (b.connections > 0);
    });
    return out;
}

// ---------------------------------------------------------------- PortTracker

PortTracker::PortTracker()
{
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

PortTracker::~PortTracker()
{
    Stop();
    if (wakeEvent_) CloseHandle(wakeEvent_);
}

void PortTracker::Stop()
{
    stop_ = true;
    if (wakeEvent_) SetEvent(wakeEvent_);
    if (thread_.joinable()) thread_.join();
}

void PortTracker::SetTarget(const std::wstring& exeName)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target_ = ToLower(exeName);
        ++generation_;
        pids_.clear();
        const std::vector<std::uint64_t> empty(lag::PortBits<int>::kWords);
        Publish(empty, empty);
        processCount_ = 0;
        portCount_ = 0;
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void PortTracker::Run()
{
    int tick = 0;
    while (!stop_) {
        Refresh(tick++);
        WaitForSingleObject(wakeEvent_, 300);
    }
}

void PortTracker::Refresh(int tick)
{
    std::wstring target;
    std::uint64_t generation;
    std::vector<unsigned long> pids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        target = target_;
        generation = generation_;
        pids = pids_;
    }
    if (target.empty()) return;

    // La liste des processus change rarement : on la relit toutes les ~2 s.
    if (pids.empty() || tick % 6 == 0) {
        pids.clear();
        ForEachProcess([&](DWORD pid, const wchar_t* exe) {
            if (ToLower(exe) == target) pids.push_back(pid);
        });
    }

    std::vector<std::uint64_t> tcp(lag::PortBits<int>::kWords), udp(lag::PortBits<int>::kWords);
    int ports = 0;
    if (!pids.empty()) {
        ForEachSocket([&](DWORD pid, bool isTcp, uint16_t port) {
            if (std::find(pids.begin(), pids.end(), pid) == pids.end()) return;
            lag::SetBit(isTcp ? tcp : udp, port);
            ++ports;
        });
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) return;  // la cible a changé entre-temps
    pids_ = pids;
    Publish(tcp, udp);
    processCount_ = static_cast<int>(pids.size());
    portCount_ = ports;
}

void PortTracker::Publish(const std::vector<std::uint64_t>& tcp, const std::vector<std::uint64_t>& udp)
{
    for (size_t i = 0; i < lag::PortBits<int>::kWords; ++i) {
        tcp_.words[i].store(tcp[i], std::memory_order_relaxed);
        udp_.words[i].store(udp[i], std::memory_order_relaxed);
    }
}
