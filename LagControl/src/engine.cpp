#include "engine.h"

#include <mmsystem.h>

#include <chrono>
#include <random>

#include "processes.h"

namespace {

const char* kFilter = "!loopback and (tcp or udp)";

double NowSec()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string LastErrorText(DWORD code)
{
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::string text = msg ? ToUtf8(msg) : std::string();
    if (msg) LocalFree(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text + " (code " + std::to_string(code) + ")";
}

}  // namespace

LagEngine::LagEngine(const PortTracker& tracker) : tracker_(tracker) {}

LagEngine::~LagEngine() { Stop(); }

std::size_t LagEngine::Queued()
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    return queue_.Size();
}

void LagEngine::ResetStats()
{
    delayed = 0;
    dropped = 0;
    passed = 0;
}

bool LagEngine::Start(std::string& error)
{
    std::lock_guard<std::mutex> op(opMutex_);
    if (running_) return true;

    HANDLE h = WinDivertOpen(kFilter, WINDIVERT_LAYER_NETWORK, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        error = "Impossible d'ouvrir le pilote WinDivert.\n\n";
        if (code == ERROR_ACCESS_DENIED)
            error += "Lancez LagControl en tant qu'administrateur.";
        else if (code == ERROR_FILE_NOT_FOUND)
            error += "WinDivert.dll / WinDivert64.sys doivent être à côté de LagControl.exe.";
        else
            error += LastErrorText(code);
        return false;
    }
    // Files du pilote plus grandes : rien n'est perdu même à fort débit.
    WinDivertSetParam(h, WINDIVERT_PARAM_QUEUE_LENGTH, 16384);
    WinDivertSetParam(h, WINDIVERT_PARAM_QUEUE_TIME, 8000);

    timeBeginPeriod(1);  // attentes précises à 1 ms au lieu de ~15 ms
    handle_ = h;
    faulted_ = false;
    clock_.Reset();
    running_ = true;
    recvThread_ = std::thread([this] { RecvLoop(); });
    sendThread_ = std::thread([this] { SendLoop(); });
    return true;
}

void LagEngine::Stop()
{
    std::lock_guard<std::mutex> op(opMutex_);
    if (!running_ && handle_ == INVALID_HANDLE_VALUE) return;

    running_ = false;
    Flush();                                           // 1. relâcher la file
    WinDivertShutdown(handle_, WINDIVERT_SHUTDOWN_RECV);  // 2. la capture se termine
    queueCv_.notify_all();
    if (recvThread_.joinable()) recvThread_.join();
    if (sendThread_.joinable()) sendThread_.join();
    Flush();                                           // 3. paquet arrivé pendant l'arrêt
    WinDivertClose(handle_);                           // 4. réseau normal
    handle_ = INVALID_HANDLE_VALUE;
    faulted_ = false;
    timeEndPeriod(1);
}

std::size_t LagEngine::Flush()
{
    std::vector<Packet> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.DrainAll(pending);
    }
    for (auto& p : pending) SendRaw(p.data.data(), static_cast<UINT>(p.data.size()), p.addr);
    return pending.size();
}

void LagEngine::SendRaw(const unsigned char* data, UINT len, const WINDIVERT_ADDRESS& addr)
{
    WinDivertSend(handle_, data, len, nullptr, &addr);
}

void LagEngine::RecvLoop()
{
    std::vector<unsigned char> buf(WINDIVERT_MTU_MAX);
    int errors = 0;
    for (;;) {
        WINDIVERT_ADDRESS addr{};
        UINT len = 0;
        if (!WinDivertRecv(handle_, buf.data(), static_cast<UINT>(buf.size()), &len, &addr)) {
            DWORD code = GetLastError();
            if (code == ERROR_NO_DATA || code == ERROR_OPERATION_ABORTED || code == ERROR_INVALID_HANDLE)
                return;  // arrêt demandé
            if (code == ERROR_INSUFFICIENT_BUFFER) continue;
            if (++errors > 200) {
                // Pilote en échec : on arrête de capturer, l'interface appellera Stop().
                running_ = false;
                faulted_ = true;
                queueCv_.notify_all();
                return;
            }
            continue;
        }
        errors = 0;
        Process(buf.data(), len, addr);
    }
}

void LagEngine::Process(const unsigned char* data, UINT len, const WINDIVERT_ADDRESS& addr)
{
    PWINDIVERT_TCPHDR tcp = nullptr;
    PWINDIVERT_UDPHDR udp = nullptr;
    WinDivertHelperParsePacket(data, len, nullptr, nullptr, nullptr, nullptr, nullptr, &tcp, &udp, nullptr, nullptr,
                               nullptr, nullptr);

    const bool outbound = addr.Outbound != 0;
    bool targeted = false;
    if (running_ && lag::DirectionMatches(direction.load(), outbound)) {
        if (tcp) {
            UINT16 port = WinDivertHelperNtohs(outbound ? tcp->SrcPort : tcp->DstPort);
            targeted = tracker_.HasTcp(port);
        } else if (udp) {
            UINT16 port = WinDivertHelperNtohs(outbound ? udp->SrcPort : udp->DstPort);
            targeted = tracker_.HasUdp(port);
        }
    }
    if (!targeted) {
        SendRaw(data, len, addr);
        ++passed;
        return;
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    const float loss = lossPct.load();
    if (loss > 0.0f && std::uniform_real_distribution<float>(0.0f, 100.0f)(rng) < loss) {
        ++dropped;
        return;
    }

    double delay = delayMs.load();
    if (int j = jitterMs.load(); j > 0) delay += std::uniform_real_distribution<double>(-j, j)(rng);

    const double now = NowSec();
    const double release = clock_.Next(now, delay, outbound);
    if (release <= now) {
        SendRaw(data, len, addr);
        ++passed;
        return;
    }

    Packet p;
    p.data.assign(data, data + len);
    p.addr = addr;
    bool earliest;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        earliest = queue_.Empty() || release < queue_.NextRelease();
        queue_.Push(std::move(p), release);
    }
    if (earliest) queueCv_.notify_one();
    ++delayed;
}

void LagEngine::SendLoop()
{
    std::vector<Packet> due;
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            for (;;) {
                if (!running_) return;
                if (queue_.Empty()) {
                    queueCv_.wait(lock);
                    continue;
                }
                const double wait = queue_.NextRelease() - NowSec();
                if (wait <= 0) break;
                queueCv_.wait_for(lock, std::chrono::duration<double>(wait));
            }
            queue_.PopDue(NowSec(), due);
        }
        for (auto& p : due) SendRaw(p.data.data(), static_cast<UINT>(p.data.size()), p.addr);
        due.clear();
    }
}
