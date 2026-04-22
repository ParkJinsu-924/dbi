#pragma once

#include "Network/IoContextPool.h"
#include "StressConfig.h"

#include <atomic>
#include <fstream>
#include <memory>
#include <vector>


class StressBot;


// 300봇을 ramp-up 스케줄대로 띄우고 holdSec 동안 유지 후 종료.
// 내부에 CSV 리포터 스레드를 포함.
class StressHarness
{
public:
    explicit StressHarness(const StressConfig& cfg);
    ~StressHarness();

    int Run();

private:
    void SpawnBots();
    void ReporterLoop(std::stop_token st);
    void WriteCsvHeader();
    void WriteCsvRow(long long elapsedSec);

    StressConfig cfg_;
    IoContextPool ioPool_;

    std::vector<std::shared_ptr<StressBot>> bots_;
    std::jthread reporterThread_;
    std::ofstream csv_;
    std::chrono::steady_clock::time_point startedAt_;
};
