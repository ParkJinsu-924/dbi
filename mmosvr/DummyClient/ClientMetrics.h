#pragma once

#include "Utils/Metrics.h"
#include <atomic>


// 부하 클라 전역 메트릭. 세션/메시지 스레드 어디서든 갱신 가능.
namespace ClientMetrics
{
    // FSM 진행 카운터
    inline Metrics::Counter loginAttempted;
    inline Metrics::Counter loginSucceeded;
    inline Metrics::Counter loginFailed;
    inline Metrics::Counter gameConnectAttempted;
    inline Metrics::Counter gameConnectSucceeded;
    inline Metrics::Counter gameConnectFailed;
    inline Metrics::Counter enterGameSucceeded;
    inline Metrics::Counter enterGameFailed;
    inline Metrics::Counter disconnects;

    // I/O 카운터는 ServerCore/Session 계층이 ServerMetrics:: 로 집계하므로
    // DummyClient 프로세스에서는 ServerMetrics::packetsSent 등을 그대로 재사용한다.

    // RTT 히스토그램 (클라 → 서버 → 클라 에코)
    inline Metrics::Histogram moveRttUs;

    // 현재 게이지
    inline std::atomic<int> botsTotal{0};
    inline std::atomic<int> botsConnected{0};
    inline std::atomic<int> botsInGame{0};
}
