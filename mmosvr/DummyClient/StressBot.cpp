#include "pch.h"
#include "StressBot.h"
#include "ClientMetrics.h"
#include "Packet/PacketUtils.h"

#include "login.pb.h"
#include "game.pb.h"

#include <chrono>
#include <cmath>


namespace
{
    // bot 당 고유 ID → username/seed 생성.
    std::string MakeUsername(int idx)
    {
        return "bot" + std::to_string(100000 + idx);
    }

    Proto::Vector2 MakeVec2(float x, float y)
    {
        Proto::Vector2 v; v.set_x(x); v.set_y(y); return v;
    }
}


StressBot::StressBot(int botIndex, net::io_context& ioc, const StressConfig& cfg)
    : botIndex_(botIndex)
    , ioc_(ioc)
    , cfg_(cfg)
    , username_(MakeUsername(botIndex))
    , rng_(static_cast<std::minstd_rand::result_type>(
        std::chrono::steady_clock::now().time_since_epoch().count() + botIndex))
    , moveTimer_(ioc)
{
    pos_ = MakeVec2(0.0f, 0.0f);
}

void StressBot::Start()
{
    ConnectLogin();
}

void StressBot::Stop()
{
    try { moveTimer_.cancel(); } catch (...) {}
    if (loginSession_) loginSession_->Disconnect();
    if (gameSession_)  gameSession_->Disconnect();
}

// ─── Login phase ────────────────────────────────────────────────────

void StressBot::ConnectLogin()
{
    SetState(State::LoginConnecting);
    ClientMetrics::loginAttempted.Add();

    tcp::endpoint endpoint(net::ip::make_address(cfg_.host),
        static_cast<uint16_t>(cfg_.loginPort));

    auto session = std::make_shared<BotSession>(tcp::socket(ioc_), ioc_);
    loginSession_ = session;

    auto self = shared_from_this();
    session->SetRecvCallback(
        [self](std::uint16_t id, const char* buf, std::int32_t len)
        {
            self->HandleLoginPacket(id, buf, len);
        });

    session->GetSocket().async_connect(endpoint,
        [self, session](boost::system::error_code ec)
        {
            if (ec)
            {
                ClientMetrics::loginFailed.Add();
                self->Die();
                return;
            }
            if (!self->countedConnected_)
            {
                ClientMetrics::botsConnected.fetch_add(1, std::memory_order_relaxed);
                self->countedConnected_ = true;
            }
            session->Start();
            self->SendLoginRequest();
        });
}

void StressBot::SendLoginRequest()
{
    SetState(State::LoginWaiting);

    Proto::C_Login req;
    req.set_username(username_);
    req.set_password("stresspass");
    loginSession_->Send(req);
}

void StressBot::HandleLoginPacket(std::uint16_t packetId, const char* payload, std::int32_t size)
{
    switch (static_cast<PacketId>(packetId))
    {
    case PacketId::S_LOGIN:
    {
        Proto::S_Login pkt;
        if (!pkt.ParseFromArray(payload, size))
        {
            ClientMetrics::loginFailed.Add();
            Die();
            return;
        }
        token_ = pkt.token();
        ClientMetrics::loginSucceeded.Add();

        std::string ip = pkt.game_server_ip().empty() ? cfg_.host : pkt.game_server_ip();
        int port = pkt.game_server_port() > 0 ? pkt.game_server_port() : cfg_.gamePort;

        // LoginServer 세션 종료 후 GameServer 연결 — async 경계에서 전환.
        loginSession_->Disconnect();
        loginSession_.reset();

        ConnectGame(ip, port);
        break;
    }
    case PacketId::S_ERROR:
    {
        Proto::S_Error pkt;
        pkt.ParseFromArray(payload, size);
        ClientMetrics::loginFailed.Add();
        Die();
        break;
    }
    default:
        // 알 수 없는 패킷은 무시 (부하 상황 스펨 방지).
        break;
    }
}

// ─── Game phase ─────────────────────────────────────────────────────

void StressBot::ConnectGame(const std::string& ip, int port)
{
    SetState(State::GameConnecting);
    ClientMetrics::gameConnectAttempted.Add();

    boost::system::error_code ecAddr;
    auto addr = net::ip::make_address(ip, ecAddr);
    if (ecAddr)
    {
        ClientMetrics::gameConnectFailed.Add();
        Die();
        return;
    }

    tcp::endpoint endpoint(addr, static_cast<uint16_t>(port));

    auto session = std::make_shared<BotSession>(tcp::socket(ioc_), ioc_);
    gameSession_ = session;

    auto self = shared_from_this();
    session->SetRecvCallback(
        [self](std::uint16_t id, const char* buf, std::int32_t len)
        {
            self->HandleGamePacket(id, buf, len);
        });

    session->GetSocket().async_connect(endpoint,
        [self, session](boost::system::error_code ec)
        {
            if (ec)
            {
                ClientMetrics::gameConnectFailed.Add();
                self->Die();
                return;
            }
            ClientMetrics::gameConnectSucceeded.Add();
            session->Start();
            self->SendEnterGame();
        });
}

void StressBot::SendEnterGame()
{
    SetState(State::GameWaiting);

    Proto::C_EnterGame req;
    req.set_token(token_);
    gameSession_->Send(req);
}

void StressBot::HandleGamePacket(std::uint16_t packetId, const char* payload, std::int32_t size)
{
    switch (static_cast<PacketId>(packetId))
    {
    case PacketId::S_ENTER_GAME:
    {
        Proto::S_EnterGame pkt;
        if (!pkt.ParseFromArray(payload, size))
        {
            ClientMetrics::enterGameFailed.Add();
            Die();
            return;
        }
        myPlayerId_ = pkt.player_id();
        myGuid_     = pkt.guid();
        pos_ = pkt.spawn_position();
        ClientMetrics::enterGameSucceeded.Add();

        if (!countedInGame_)
        {
            ClientMetrics::botsInGame.fetch_add(1, std::memory_order_relaxed);
            countedInGame_ = true;
        }
        SetState(State::InGame);
        ScheduleMove();
        break;
    }
    case PacketId::S_UNIT_POSITIONS:
    {
        if (!lastMoveSentAtValid_) break;
        Proto::S_UnitPositions pkt;
        if (!pkt.ParseFromArray(payload, size)) break;
        // snapshot 내 내 guid 발견되면 "command 반영 지연" 으로 RTT 기록.
        for (const auto& u : pkt.units())
        {
            if (u.guid() == myGuid_)
            {
                const auto rttUs = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - lastMoveSentAt_).count();
                ClientMetrics::moveRttUs.Observe(static_cast<std::uint64_t>(rttUs));
                lastMoveSentAtValid_ = false;
                break;
            }
        }
        break;
    }
    case PacketId::S_ERROR:
    {
        Proto::S_Error pkt;
        pkt.ParseFromArray(payload, size);
        if (pkt.source_packet_id() == static_cast<uint32_t>(PacketId::C_ENTER_GAME))
        {
            ClientMetrics::enterGameFailed.Add();
            Die();
        }
        break;
    }
    default:
        // S_PlayerSpawn / S_MonsterList / S_MonsterMove / S_SkillHit 등은 수신 카운터만.
        break;
    }
}

// ─── Movement loop ──────────────────────────────────────────────────

void StressBot::ScheduleMove()
{
    if (state_.load(std::memory_order_relaxed) != State::InGame) return;

    moveTimer_.expires_after(std::chrono::milliseconds(cfg_.movePeriodMs));

    auto self = shared_from_this();
    moveTimer_.async_wait([self](boost::system::error_code ec)
        {
            if (ec) return;
            if (self->state_.load(std::memory_order_relaxed) != State::InGame) return;
            self->DoMove();
            self->ScheduleMove();
        });
}

void StressBot::DoMove()
{
    if (!gameSession_ || !gameSession_->IsConnected()) { Die(); return; }

    // 원점 근처 반경 3~10m 랜덤 target 으로 C_MoveCommand 송신.
    // 서버 측 C_MoveCommand 는 클라 권위 이동 전환(2026-04-23) 이후 no-op 이라
    // 실제 위치 갱신은 일어나지 않는다 — DB/세션 부하 측정용 idle traffic 으로만 의미.
    std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> radius(3.0f, 10.0f);
    const float a = angle(rng_);
    const float r = radius(rng_);

    Proto::C_MoveCommand req;
    req.mutable_target_pos()->set_x(std::cos(a) * r);
    req.mutable_target_pos()->set_y(std::sin(a) * r);

    lastMoveSentAt_ = std::chrono::steady_clock::now();
    lastMoveSentAtValid_ = true;

    gameSession_->Send(req);
}

// ─── Termination ────────────────────────────────────────────────────

void StressBot::Die()
{
    if (state_.load(std::memory_order_relaxed) == State::Dead) return;

    try { moveTimer_.cancel(); } catch (...) {}

    if (loginSession_) { loginSession_->Disconnect(); loginSession_.reset(); }
    if (gameSession_)  { gameSession_->Disconnect();  gameSession_.reset();  }

    if (countedInGame_)
    {
        ClientMetrics::botsInGame.fetch_sub(1, std::memory_order_relaxed);
        countedInGame_ = false;
    }
    if (countedConnected_)
    {
        ClientMetrics::botsConnected.fetch_sub(1, std::memory_order_relaxed);
        countedConnected_ = false;
    }
    ClientMetrics::disconnects.Add();

    SetState(State::Dead);
}
