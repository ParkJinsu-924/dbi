#include "pch.h"
#include "Utils/MetricsReporter.h"
#include "Utils/Metrics.h"

#include <filesystem>
#include <sstream>


MetricsReporter::~MetricsReporter()
{
    Stop();
}

void MetricsReporter::Start(const std::string& csvPath,
    std::chrono::seconds period,
    int tickBudgetUs)
{
    Stop();

    period_       = period;
    tickBudgetUs_ = tickBudgetUs;
    startedAt_    = std::chrono::steady_clock::now();

    if (!csvPath.empty())
    {
        std::error_code ec;
        const auto parent = std::filesystem::path(csvPath).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);

        csv_.open(csvPath, std::ios::out | std::ios::trunc);
        if (csv_.is_open())
            WriteHeader();
        else
            LOG_WARN("MetricsReporter: failed to open CSV: " + csvPath);
    }

    thread_ = std::jthread([this](std::stop_token st) { Loop(st); });

    LOG_INFO("MetricsReporter started (period=" + std::to_string(period_.count()) + "s"
        + (csvPath.empty() ? "" : ", csv=" + csvPath) + ")");
}

void MetricsReporter::Stop()
{
    if (thread_.joinable())
    {
        thread_.request_stop();
        thread_.join();
    }
    if (csv_.is_open())
    {
        csv_.flush();
        csv_.close();
    }
}

void MetricsReporter::WriteHeader()
{
    csv_ <<
        "elapsed_sec,sessions,players,monsters,projectiles,"
        "tick_count,tick_mean_us,tick_p50_us,tick_p95_us,tick_p99_us,tick_max_us,tick_over_budget,"
        "zone_update_p95_us,zone_update_max_us,"
        "broadcast_p95_us,broadcast_max_us,"
        "packet_flush_p95_us,packet_flush_max_us,"
        "packets_sent,bytes_sent,packets_recv,bytes_recv,send_calls\n";
    csv_.flush();
}

void MetricsReporter::Loop(std::stop_token st)
{
    using Clock = std::chrono::steady_clock;
    auto next = Clock::now() + period_;

    while (!st.stop_requested())
    {
        // 취소 감응형 sleep — stop_requested 즉시 깸.
        const auto now = Clock::now();
        if (now < next)
        {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(next - now);
            // 100ms 단위로 쪼개서 깨어남 — stop_token 감지 지연 최소화.
            auto sleepEnd = now + remaining;
            while (!st.stop_requested() && Clock::now() < sleepEnd)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (st.stop_requested()) break;

        WriteRow();

        next += period_;
        if (next < Clock::now())
            next = Clock::now() + period_;
    }
}

void MetricsReporter::WriteRow()
{
    using namespace std::chrono;

    const auto elapsed = duration_cast<seconds>(steady_clock::now() - startedAt_).count();

    auto tickSnap  = ServerMetrics::tickTimeUs.SnapshotAndReset();
    auto zoneSnap  = ServerMetrics::zoneUpdateUs.SnapshotAndReset();
    auto bcastSnap = ServerMetrics::broadcastUs.SnapshotAndReset();
    auto flushSnap = ServerMetrics::packetFlushUs.SnapshotAndReset();

    const auto packetsSent = ServerMetrics::packetsSent.Exchange();
    const auto bytesSent   = ServerMetrics::bytesSent.Exchange();
    const auto packetsRecv = ServerMetrics::packetsRecv.Exchange();
    const auto bytesRecv   = ServerMetrics::bytesRecv.Exchange();
    const auto sendCalls   = ServerMetrics::sendCalls.Exchange();
    const auto overBudget  = ServerMetrics::tickOverBudget.Exchange();

    const int sessions    = ServerMetrics::currentSessions.load(std::memory_order_relaxed);
    const int players     = ServerMetrics::currentPlayers.load(std::memory_order_relaxed);
    const int monsters    = ServerMetrics::currentMonsters.load(std::memory_order_relaxed);
    const int projectiles = ServerMetrics::currentProjectiles.load(std::memory_order_relaxed);

    const double tickP95Ms  = tickSnap.PercentileUs(0.95)  / 1000.0;
    const double tickMaxMs  = static_cast<double>(tickSnap.maxUs) / 1000.0;
    const double bcastP95Ms = bcastSnap.PercentileUs(0.95) / 1000.0;

    std::ostringstream os;
    os << "[METRICS t=" << elapsed << "s]"
       << " sess=" << sessions
       << " pl=" << players
       << " mon=" << monsters
       << " proj=" << projectiles
       << " | tick p95=" << tickP95Ms << "ms max=" << tickMaxMs << "ms over=" << overBudget
       << " | bcast p95=" << bcastP95Ms << "ms"
       << " | tx=" << packetsSent << " pkt / " << (bytesSent / 1024) << " KB"
       << " rx=" << packetsRecv << " pkt / " << (bytesRecv / 1024) << " KB";
    LOG_INFO(os.str());

    if (csv_.is_open())
    {
        csv_ << elapsed
             << ',' << sessions
             << ',' << players
             << ',' << monsters
             << ',' << projectiles
             << ',' << tickSnap.count
             << ',' << tickSnap.MeanUs()
             << ',' << tickSnap.PercentileUs(0.50)
             << ',' << tickSnap.PercentileUs(0.95)
             << ',' << tickSnap.PercentileUs(0.99)
             << ',' << tickSnap.maxUs
             << ',' << overBudget
             << ',' << zoneSnap.PercentileUs(0.95)
             << ',' << zoneSnap.maxUs
             << ',' << bcastSnap.PercentileUs(0.95)
             << ',' << bcastSnap.maxUs
             << ',' << flushSnap.PercentileUs(0.95)
             << ',' << flushSnap.maxUs
             << ',' << packetsSent
             << ',' << bytesSent
             << ',' << packetsRecv
             << ',' << bytesRecv
             << ',' << sendCalls
             << '\n';
        csv_.flush();
    }
}
