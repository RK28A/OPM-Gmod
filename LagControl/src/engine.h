// Moteur de latence : intercepte les paquets avec WinDivert et retarde
// uniquement ceux dont le port local appartient à l'application ciblée.
//
//  * un thread de capture reçoit chaque paquet TCP/UDP de la machine ;
//  * paquet de la cible -> file d'attente triée par heure de sortie ;
//    autre paquet       -> réinjecté immédiatement ;
//  * un thread d'envoi réinjecte les paquets retardés à l'heure prévue.
//
// Fermer le handle WinDivert rend aussitôt le réseau normal : c'est ce que font
// Stop() et l'arrêt d'urgence, après avoir relâché les paquets en attente.
// Si le programme plante, Windows ferme le handle tout seul.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <windivert.h>
#ifndef _MSC_VER
// windivert.h définit ces annotations SAL vides hors MSVC ; elles cassent
// ensuite les en-têtes de libstdc++ (MinGW) qui utilisent « __out » comme nom.
#undef __in
#undef __in_opt
#undef __out
#undef __out_opt
#undef __inout
#undef __inout_opt
#endif

#include "lag_core.h"

class PortTracker;

class LagEngine {
public:
    explicit LagEngine(const PortTracker& tracker);
    ~LagEngine();
    LagEngine(const LagEngine&) = delete;
    LagEngine& operator=(const LagEngine&) = delete;

    bool Start(std::string& error);
    void Stop();  // relâche tout, ferme le pilote ; sans effet si inactif
    bool Active() const { return running_.load(); }
    // Le pilote a lâché en cours de route : appeler Stop() pour rétablir le réseau.
    bool Faulted() const { return faulted_.load(); }

    // Réglages lus à chaque paquet, modifiables en direct.
    std::atomic<int> delayMs{300};
    std::atomic<int> jitterMs{0};
    std::atomic<float> lossPct{0.0f};
    std::atomic<int> direction{lag::DirBoth};

    std::atomic<std::uint64_t> delayed{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> passed{0};
    std::size_t Queued();
    void ResetStats();

private:
    struct Packet {
        std::vector<unsigned char> data;
        WINDIVERT_ADDRESS addr;
    };

    void RecvLoop();
    void SendLoop();
    void Process(const unsigned char* data, UINT len, const WINDIVERT_ADDRESS& addr);
    void SendRaw(const unsigned char* data, UINT len, const WINDIVERT_ADDRESS& addr);
    std::size_t Flush();

    const PortTracker& tracker_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> running_{false};
    std::atomic<bool> faulted_{false};
    std::mutex opMutex_;

    std::mutex queueMutex_;
    std::condition_variable queueCv_;
    lag::DelayQueue<Packet> queue_;
    lag::ReleaseClock clock_;  // utilisé uniquement par le thread de capture

    std::thread recvThread_;
    std::thread sendThread_;
};
