// Tests de la logique portable de LagControl (aucune dépendance Windows).
//   g++ -std=c++17 -Wall -Wextra -Werror -I../src core_tests.cpp -o core_tests && ./core_tests
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

#include "lag_core.h"

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                               \
    do {                                                                          \
        ++g_checks;                                                               \
        if (!(cond)) {                                                            \
            ++g_failures;                                                         \
            std::printf("ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                         \
    } while (0)

static void TestDirection()
{
    CHECK(lag::DirectionMatches(lag::DirBoth, true));
    CHECK(lag::DirectionMatches(lag::DirBoth, false));
    CHECK(lag::DirectionMatches(lag::DirOut, true));
    CHECK(!lag::DirectionMatches(lag::DirOut, false));
    CHECK(lag::DirectionMatches(lag::DirIn, false));
    CHECK(!lag::DirectionMatches(lag::DirIn, true));
    CHECK(lag::AddedPingMs(300, lag::DirBoth) == 600);
    CHECK(lag::AddedPingMs(300, lag::DirIn) == 300);
}

static void TestReleaseClockKeepsOrder()
{
    lag::ReleaseClock clock;
    // Un paquet à 500 ms puis un à 100 ms : le second ne doit pas doubler le premier.
    double a = clock.Next(10.0, 500, true);
    double b = clock.Next(10.001, 100, true);
    CHECK(a == 10.5);
    CHECK(b >= a);
    // L'autre sens est indépendant.
    double c = clock.Next(10.002, 100, false);
    CHECK(c < a);
    // Délai négatif (variation) ramené à zéro.
    lag::ReleaseClock fresh;
    CHECK(fresh.Next(5.0, -40, true) == 5.0);
    clock.Reset();
    CHECK(clock.Next(1.0, 0, true) == 1.0);
}

static void TestDelayQueue()
{
    lag::DelayQueue<std::string> q;
    CHECK(q.Empty());
    q.Push("c", 3.0);
    q.Push("a", 1.0);
    q.Push("b", 2.0);
    q.Push("a2", 1.0);  // même heure : ordre d'arrivée conservé
    CHECK(q.Size() == 4);
    CHECK(q.NextRelease() == 1.0);

    std::vector<std::string> out;
    q.PopDue(0.5, out);
    CHECK(out.empty());
    q.PopDue(2.0, out);
    CHECK((out == std::vector<std::string>{"a", "a2", "b"}));
    CHECK(q.Size() == 1);

    out.clear();
    q.Push("d", 9.0);
    q.DrainAll(out);  // arrêt : tout part, dans l'ordre
    CHECK((out == std::vector<std::string>{"c", "d"}));
    CHECK(q.Empty());
}

static void TestPortBits()
{
    lag::PortBits<std::atomic<std::uint64_t>> bits;
    CHECK(!bits.Has(0));
    CHECK(!bits.Has(65535));
    std::vector<std::uint64_t> words(lag::PortBits<int>::kWords);
    lag::SetBit(words, 0);
    lag::SetBit(words, 27015);
    lag::SetBit(words, 65535);
    for (std::size_t i = 0; i < words.size(); ++i) bits.words[i] = words[i];
    CHECK(bits.Has(0));
    CHECK(bits.Has(27015));
    CHECK(bits.Has(65535));
    CHECK(!bits.Has(27016));
    CHECK(!bits.Has(443));
}

int main()
{
    TestDirection();
    TestReleaseClockKeepsOrder();
    TestDelayQueue();
    TestPortBits();
    std::printf("%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
