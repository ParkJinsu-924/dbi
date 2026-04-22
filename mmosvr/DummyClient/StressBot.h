#pragma once

#include "Network/PacketSession.h"
#include "StressConfig.h"

#include <atomic>
#include <memory>
#include <random>
#include <string>


// 콜백 기반 세션 — BotSession 1개가 LoginServer 또는 GameServer 한 쪽에 붙는다.
// Bot 은 LoginPhase / GamePhase 에 각각 새로운 BotSession 을 만든다.
class BotSession : public PacketSession
{
public:
    using RecvFn = std::function<void(std::uint16_t, const char*, std::int32_t)>;
    using PacketSession::PacketSession;

    void SetRecvCallback(RecvFn cb) { cb_ = std::move(cb); }

protected:
    void OnRecvPacket(std::uint16_t packetId, const char* payload, std::int32_t size) override
    {
        if (cb_) cb_(packetId, payload, size);
    }

private:
    RecvFn cb_;
};


// 부하 봇 1개. 상태 기계:
//   Idle → LoginConnecting → LoginWaiting → GameConnecting → GameWaiting → InGame
//   (실패 또는 Disconnect → Dead)
class StressBot : public std::enable_shared_from_this<StressBot>
{
public:
    enum class State { Idle, LoginConnecting, LoginWaiting, GameConnecting, GameWaiting, InGame, Dead };

    StressBot(int botIndex, net::io_context& ioc, const StressConfig& cfg);

    void Start();  // ConnectLogin 시작
    void Stop();   // 세션 정리

    State GetState() const noexcept { return state_.load(std::memory_order_relaxed); }

private:
    void ConnectLogin();
    void SendLoginRequest();
    void HandleLoginPacket(std::uint16_t packetId, const char* payload, std::int32_t size);

    void ConnectGame(const std::string& ip, int port);
    void SendEnterGame();
    void HandleGamePacket(std::uint16_t packetId, const char* payload, std::int32_t size);

    void ScheduleMove();
    void DoMove();

    void SetState(State s) noexcept { state_.store(s, std::memory_order_relaxed); }
    void TransitionTo(State next, bool wasConnected);  // gauge 갱신 포함.
    void Die();

    int botIndex_;
    net::io_context& ioc_;
    StressConfig cfg_;

    std::atomic<State> state_{State::Idle};
    std::string username_;
    std::string token_;
    int32_t myPlayerId_ = 0;
    int64_t myGuid_     = 0;

    Proto::Vector2 pos_{};
    std::minstd_rand rng_;

    // RTT 측정 — 직전 C_PlayerMove 송신 시각.
    // FIFO 가정: 이전 에코보다 더 최근 에코가 후속 이동의 RTT 를 덮어쓰게 되면
    // 실제보다 작게 측정될 수 있지만, 200ms 주기 기준 오버랩은 드물다고 가정.
    std::chrono::steady_clock::time_point lastMoveSentAt_{};
    bool lastMoveSentAtValid_ = false;

    std::shared_ptr<BotSession> loginSession_;
    std::shared_ptr<BotSession> gameSession_;
    net::steady_timer moveTimer_;

    // gauge: Connected / InGame 이중 증감 방지용 플래그
    bool countedConnected_ = false;
    bool countedInGame_    = false;
};
