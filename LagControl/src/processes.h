// Liste des applications et suivi des ports réseau de l'application ciblée.
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "lag_core.h"

struct AppInfo {
    std::wstring name;   // tel qu'affiché par Windows, ex. "gmod.exe"
    std::wstring key;    // en minuscules, pour comparer
    std::string utf8;    // pour l'interface
    std::vector<unsigned long> pids;
    int connections = 0;
};

std::wstring ToLower(std::wstring s);
std::string ToUtf8(const std::wstring& s);
std::wstring FromUtf8(const std::string& s);

// Applications en cours, regroupées par nom d'exécutable (connectées d'abord).
std::vector<AppInfo> ListApps();

// Relève en arrière-plan, plusieurs fois par seconde, les ports locaux ouverts
// par tous les processus portant le nom ciblé.
class PortTracker {
public:
    PortTracker();
    ~PortTracker();
    PortTracker(const PortTracker&) = delete;
    PortTracker& operator=(const PortTracker&) = delete;

    void SetTarget(const std::wstring& exeName);
    void Stop();

    bool HasTcp(std::uint16_t port) const { return tcp_.Has(port); }
    bool HasUdp(std::uint16_t port) const { return udp_.Has(port); }
    int ProcessCount() const { return processCount_.load(); }
    int PortCount() const { return portCount_.load(); }

private:
    void Run();
    void Refresh(int tick);
    void Publish(const std::vector<std::uint64_t>& tcp, const std::vector<std::uint64_t>& udp);

    lag::PortBits<std::atomic<std::uint64_t>> tcp_;
    lag::PortBits<std::atomic<std::uint64_t>> udp_;
    std::atomic<int> processCount_{0};
    std::atomic<int> portCount_{0};

    std::mutex mutex_;
    std::wstring target_;
    std::uint64_t generation_ = 0;
    std::vector<unsigned long> pids_;

    std::atomic<bool> stop_{false};
    void* wakeEvent_ = nullptr;
    std::thread thread_;
};
