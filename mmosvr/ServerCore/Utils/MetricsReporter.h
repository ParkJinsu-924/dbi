#pragma once

#include <chrono>
#include <fstream>
#include <string>


// Periodic CSV dumper for ServerMetrics. Safe to Start once at server init
// and Stop at shutdown. Dumps one row per period, then resets all histograms/counters.
class MetricsReporter
{
public:
    MetricsReporter() = default;
    ~MetricsReporter();

    // csvPath : 빈 문자열이면 콘솔만.
    // period  : 덤프 주기 (기본 5초).
    // tickBudgetUs : tick 초과 카운트 기준 (기본 10ms = 100Hz).
    void Start(const std::string& csvPath,
        std::chrono::seconds period = std::chrono::seconds(5),
        int tickBudgetUs = 10000);

    void Stop();

private:
    void Loop(std::stop_token st);
    void WriteHeader();
    void WriteRow();

    std::jthread       thread_;
    std::ofstream      csv_;
    std::chrono::seconds period_{5};
    int                tickBudgetUs_ = 10000;
    std::chrono::steady_clock::time_point startedAt_;
};
