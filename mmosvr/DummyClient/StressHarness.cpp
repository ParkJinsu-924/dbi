#include "pch.h"
#include "StressHarness.h"
#include "StressBot.h"
#include "ClientMetrics.h"

#include <filesystem>
#include <sstream>


StressHarness::StressHarness(const StressConfig& cfg)
    : cfg_(cfg)
    , ioPool_(cfg.ioThreads)
{
}

StressHarness::~StressHarness() = default;

int StressHarness::Run()
{
    LOG_INFO("StressHarness: bots=" + std::to_string(cfg_.bots)
        + " ramp=" + std::to_string(cfg_.rampSec) + "s"
        + " hold=" + std::to_string(cfg_.holdSec) + "s"
        + " host=" + cfg_.host
        + " login=" + std::to_string(cfg_.loginPort)
        + " game="  + std::to_string(cfg_.gamePort));

    // CSV open
    if (!cfg_.csvPath.empty())
    {
        std::error_code ec;
        const auto parent = std::filesystem::path(cfg_.csvPath).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);

        csv_.open(cfg_.csvPath, std::ios::out | std::ios::trunc);
        if (csv_.is_open()) WriteCsvHeader();
        else LOG_WARN("Stress CSV open failed: " + cfg_.csvPath);
    }

    ioPool_.Run();
    startedAt_ = std::chrono::steady_clock::now();

    ClientMetrics::botsTotal.store(cfg_.bots, std::memory_order_relaxed);

    reporterThread_ = std::jthread([this](std::stop_token st) { ReporterLoop(st); });

    // Ramp-up: rampSec 동안 균등 간격으로 봇 start.
    SpawnBots();

    // Hold for rampSec + holdSec total.
    const int totalSec = cfg_.rampSec + cfg_.holdSec;
    std::this_thread::sleep_for(std::chrono::seconds(totalSec));

    LOG_INFO("StressHarness: stopping bots");
    for (auto& bot : bots_) bot->Stop();

    // 잔여 I/O 처리 시간 부여.
    std::this_thread::sleep_for(std::chrono::seconds(2));

    ioPool_.Stop();
    reporterThread_.request_stop();
    if (reporterThread_.joinable()) reporterThread_.join();
    if (csv_.is_open()) { csv_.flush(); csv_.close(); }

    LOG_INFO("StressHarness: done");
    return 0;
}

void StressHarness::SpawnBots()
{
    bots_.reserve(cfg_.bots);

    const int bots = cfg_.bots;
    // Windows.h 의 max 매크로와 충돌 방지용 괄호.
    const int rampMs = (std::max)(1, cfg_.rampSec * 1000);
    const int stepMs = (std::max)(1, rampMs / (std::max)(1, bots));

    auto startAt = std::chrono::steady_clock::now();

    for (int i = 0; i < bots; ++i)
    {
        const auto target = startAt + std::chrono::milliseconds(stepMs * i);
        std::this_thread::sleep_until(target);

        auto& ioc = ioPool_.GetNextIoContext();
        auto bot = std::make_shared<StressBot>(i, ioc, cfg_);
        bots_.push_back(bot);

        auto b = bot;
        net::post(ioc, [b]() { b->Start(); });
    }
}

// ─── Reporter ──────────────────────────────────────────────────────

void StressHarness::WriteCsvHeader()
{
    csv_ <<
        "elapsed_sec,bots_total,bots_connected,bots_in_game,"
        "login_attempted,login_succeeded,login_failed,"
        "game_connect_attempted,game_connect_succeeded,game_connect_failed,"
        "enter_game_succeeded,enter_game_failed,disconnects,"
        "rtt_count,rtt_mean_us,rtt_p50_us,rtt_p95_us,rtt_p99_us,rtt_max_us,"
        "packets_sent,bytes_sent,packets_recv,bytes_recv\n";
    csv_.flush();
}

void StressHarness::ReporterLoop(std::stop_token st)
{
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::seconds((std::max)(1, cfg_.reportSec));
    auto next = Clock::now() + period;

    while (!st.stop_requested())
    {
        while (!st.stop_requested() && Clock::now() < next)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (st.stop_requested()) break;

        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - startedAt_).count();
        WriteCsvRow(elapsed);

        next += period;
        if (next < Clock::now()) next = Clock::now() + period;
    }
}

void StressHarness::WriteCsvRow(long long elapsedSec)
{
    auto rttSnap = ClientMetrics::moveRttUs.SnapshotAndReset();

    const auto loginAtt   = ClientMetrics::loginAttempted.Exchange();
    const auto loginOk    = ClientMetrics::loginSucceeded.Exchange();
    const auto loginFail  = ClientMetrics::loginFailed.Exchange();
    const auto gcAtt      = ClientMetrics::gameConnectAttempted.Exchange();
    const auto gcOk       = ClientMetrics::gameConnectSucceeded.Exchange();
    const auto gcFail     = ClientMetrics::gameConnectFailed.Exchange();
    const auto egOk       = ClientMetrics::enterGameSucceeded.Exchange();
    const auto egFail     = ClientMetrics::enterGameFailed.Exchange();
    const auto disc       = ClientMetrics::disconnects.Exchange();
    // Session 계층이 ServerMetrics:: 로 집계 — 클라 프로세스에서는 그 카운터가 곧 Tx/Rx.
    const auto txPkt      = ServerMetrics::packetsSent.Exchange();
    const auto txBytes    = ServerMetrics::bytesSent.Exchange();
    const auto rxPkt      = ServerMetrics::packetsRecv.Exchange();
    const auto rxBytes    = ServerMetrics::bytesRecv.Exchange();

    const int bTotal = ClientMetrics::botsTotal.load(std::memory_order_relaxed);
    const int bConn  = ClientMetrics::botsConnected.load(std::memory_order_relaxed);
    const int bIG    = ClientMetrics::botsInGame.load(std::memory_order_relaxed);

    std::ostringstream os;
    os << "[CLI t=" << elapsedSec << "s]"
       << " bots=" << bConn << "/" << bTotal
       << " inGame=" << bIG
       << " | rtt p95=" << rttSnap.PercentileUs(0.95) / 1000.0 << "ms"
       << " max=" << rttSnap.maxUs / 1000.0 << "ms"
       << " | loginOk=" << loginOk << " fail=" << loginFail
       << " | enterOk=" << egOk << " fail=" << egFail
       << " | disc=" << disc;
    LOG_INFO(os.str());

    if (csv_.is_open())
    {
        csv_ << elapsedSec
             << ',' << bTotal
             << ',' << bConn
             << ',' << bIG
             << ',' << loginAtt
             << ',' << loginOk
             << ',' << loginFail
             << ',' << gcAtt
             << ',' << gcOk
             << ',' << gcFail
             << ',' << egOk
             << ',' << egFail
             << ',' << disc
             << ',' << rttSnap.count
             << ',' << rttSnap.MeanUs()
             << ',' << rttSnap.PercentileUs(0.50)
             << ',' << rttSnap.PercentileUs(0.95)
             << ',' << rttSnap.PercentileUs(0.99)
             << ',' << rttSnap.maxUs
             << ',' << txPkt
             << ',' << txBytes
             << ',' << rxPkt
             << ',' << rxBytes
             << '\n';
        csv_.flush();
    }
}
