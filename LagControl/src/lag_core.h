// Logique de retard indépendante de Windows : testable avec tests/core_tests.cpp.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace lag {

enum Direction : int { DirIn = 0, DirOut = 1, DirBoth = 2 };

inline bool DirectionMatches(int direction, bool outbound)
{
    if (direction == DirBoth) return true;
    return (direction == DirOut) == outbound;
}

// Ping supplémentaire approximatif vu par l'application (aller + retour).
inline int AddedPingMs(int delayMs, int direction)
{
    return direction == DirBoth ? delayMs * 2 : delayMs;
}

// Calcule l'heure de sortie d'un paquet. L'heure est monotone par sens, donc
// la variation (jitter) ne réordonne jamais les paquets d'un même sens.
class ReleaseClock {
public:
    double Next(double nowSec, double delayMs, bool outbound)
    {
        double& last = last_[outbound ? 1 : 0];
        double release = std::max(nowSec + std::max(0.0, delayMs) / 1000.0, last);
        last = release;
        return release;
    }
    void Reset() { last_[0] = last_[1] = 0.0; }

private:
    double last_[2] = {0.0, 0.0};
};

// File triée par heure de sortie, stable (FIFO à heure égale).
template <class T>
class DelayQueue {
public:
    void Push(T item, double releaseSec)
    {
        heap_.push(Entry{releaseSec, seq_++, std::move(item)});
    }

    bool Empty() const { return heap_.empty(); }
    std::size_t Size() const { return heap_.size(); }
    double NextRelease() const { return heap_.empty() ? 0.0 : heap_.top().release; }

    // Retire les éléments dont l'heure est passée, dans l'ordre.
    void PopDue(double nowSec, std::vector<T>& out)
    {
        while (!heap_.empty() && heap_.top().release <= nowSec) {
            out.push_back(std::move(const_cast<Entry&>(heap_.top()).item));
            heap_.pop();
        }
    }

    // Vide tout (arrêt / urgence), dans l'ordre.
    void DrainAll(std::vector<T>& out)
    {
        while (!heap_.empty()) {
            out.push_back(std::move(const_cast<Entry&>(heap_.top()).item));
            heap_.pop();
        }
    }

private:
    struct Entry {
        double release;
        std::uint64_t seq;
        T item;
    };
    struct Later {
        bool operator()(const Entry& a, const Entry& b) const
        {
            return a.release != b.release ? a.release > b.release : a.seq > b.seq;
        }
    };
    std::priority_queue<Entry, std::vector<Entry>, Later> heap_;
    std::uint64_t seq_ = 0;
};

// Ensemble de ports (0-65535) lisible sans verrou depuis le thread de capture.
template <class AtomicWord>
struct PortBits {
    static constexpr std::size_t kWords = 65536 / 64;
    AtomicWord words[kWords] = {};

    bool Has(std::uint16_t port) const
    {
        return (static_cast<std::uint64_t>(words[port >> 6]) >> (port & 63)) & 1u;
    }
};

inline void SetBit(std::vector<std::uint64_t>& words, std::uint16_t port)
{
    words[port >> 6] |= std::uint64_t{1} << (port & 63);
}

}  // namespace lag
