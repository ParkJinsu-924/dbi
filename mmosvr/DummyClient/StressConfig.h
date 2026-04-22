#pragma once

#include <string>


// 부하 봇 실행 설정. CLI 파서가 채우고 Harness 가 읽는다.
struct StressConfig
{
    std::string host        = "127.0.0.1";
    int         loginPort   = 9999;
    int         gamePort    = 7777;       // LoginServer 응답에 오는 port 를 우선 사용, fallback.
    int         bots        = 300;
    int         rampSec     = 60;
    int         holdSec     = 120;
    int         movePeriodMs = 200;
    int         ioThreads   = 4;
    std::string csvPath     = "metrics/client_metrics.csv";
    int         reportSec   = 5;
};
