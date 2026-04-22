#include "pch.h"
#include "Network/PacketSession.h"
#include "Packet/PacketUtils.h"
#include "StressConfig.h"
#include "StressHarness.h"

#include "login.pb.h"
#include "game.pb.h"

#include <charconv>
#include <cstring>


// ─── Interactive (legacy) mode ──────────────────────────────────────
// 단일 로그인 테스트. 기존 동작 그대로 유지.
class DummyClientSession : public PacketSession
{
public:
    using PacketSession::PacketSession;

    void OnConnected() override    { LOG_INFO("Connected to server!"); }
    void OnDisconnected() override { LOG_INFO("Disconnected from server."); }

    void OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize) override
    {
        switch (static_cast<PacketId>(packetId))
        {
        case PacketId::S_LOGIN:
        {
            Proto::S_Login pkt;
            pkt.ParseFromArray(payload, payloadSize);
            LOG_INFO("Login response: token=" + pkt.token()
                + " gameServer=" + pkt.game_server_ip()
                + ":" + std::to_string(pkt.game_server_port()));
            break;
        }
        case PacketId::S_ERROR:
        {
            Proto::S_Error pkt;
            pkt.ParseFromArray(payload, payloadSize);
            LOG_ERROR("Server error: source_packet_id=" + std::to_string(pkt.source_packet_id())
                + " code=" + std::to_string(static_cast<int>(pkt.code()))
                + " detail=" + pkt.detail());
            break;
        }
        default:
            break;
        }
    }
};


static int RunInteractive()
{
    try
    {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve("127.0.0.1", "9999");

        tcp::socket socket(ioc);
        net::connect(socket, endpoints);

        auto session = std::make_shared<DummyClientSession>(std::move(socket), ioc);
        session->Start();

        Proto::C_Login loginPkt;
        loginPkt.set_username("testuser");
        loginPkt.set_password("testpass");
        session->Send(loginPkt);

        LOG_INFO("Login request sent. Running event loop...");
        ioc.run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR(std::string("Error: ") + e.what());
        return 1;
    }
    return 0;
}


// ─── Argument parsing ───────────────────────────────────────────────

namespace
{
    bool StartsWith(const char* s, const char* prefix)
    {
        const auto n = std::strlen(prefix);
        return std::strncmp(s, prefix, n) == 0;
    }

    // "--key=value" 또는 "--key value" 모두 지원. 반환값: 다음 파싱 시작 인덱스.
    // 값이 없으면 empty.
    std::string ExtractValue(int argc, char** argv, int& i, const char* prefix)
    {
        const auto plen = std::strlen(prefix);
        if (std::strncmp(argv[i], prefix, plen) != 0) return {};

        // "--key=val"
        if (argv[i][plen] == '=')
            return std::string(argv[i] + plen + 1);

        // "--key" (bare). 다음 토큰이 값.
        if (argv[i][plen] == '\0' && i + 1 < argc)
        {
            return std::string(argv[++i]);
        }
        return {};
    }

    int ToInt(const std::string& s, int fallback)
    {
        int v = 0;
        auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        return ec == std::errc() ? v : fallback;
    }

    void PrintHelp()
    {
        LOG_INFO(
            "Usage:\n"
            "  DummyClient.exe                       (interactive single login)\n"
            "  DummyClient.exe --stress [options]    (load test mode)\n"
            "\n"
            "Options (stress mode):\n"
            "  --bots=N           number of bots (default 300)\n"
            "  --host=IP          server host (default 127.0.0.1)\n"
            "  --login-port=N     LoginServer port (default 9999)\n"
            "  --game-port=N      GameServer port fallback (default 7777)\n"
            "  --ramp-sec=N       seconds to linearly spawn all bots (default 60)\n"
            "  --hold-sec=N       seconds to hold after ramp (default 120)\n"
            "  --move-ms=N        per-bot move packet period ms (default 200)\n"
            "  --csv=PATH         output CSV path (default metrics/client_metrics.csv)\n"
            "  --io-threads=N     client IO threads (default 4)\n"
            "  --report-sec=N     CSV row period seconds (default 5)\n");
    }
}


int main(int argc, char** argv)
{
    LogInit();

    bool stress = false;
    StressConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--stress")              { stress = true; }
        else if (arg == "--help" || arg == "-h") { PrintHelp(); return 0; }
        else if (StartsWith(argv[i], "--bots"))        cfg.bots        = ToInt(ExtractValue(argc, argv, i, "--bots"), cfg.bots);
        else if (StartsWith(argv[i], "--host"))        cfg.host        = ExtractValue(argc, argv, i, "--host");
        else if (StartsWith(argv[i], "--login-port"))  cfg.loginPort   = ToInt(ExtractValue(argc, argv, i, "--login-port"), cfg.loginPort);
        else if (StartsWith(argv[i], "--game-port"))   cfg.gamePort    = ToInt(ExtractValue(argc, argv, i, "--game-port"), cfg.gamePort);
        else if (StartsWith(argv[i], "--ramp-sec"))    cfg.rampSec     = ToInt(ExtractValue(argc, argv, i, "--ramp-sec"), cfg.rampSec);
        else if (StartsWith(argv[i], "--hold-sec"))    cfg.holdSec     = ToInt(ExtractValue(argc, argv, i, "--hold-sec"), cfg.holdSec);
        else if (StartsWith(argv[i], "--move-ms"))     cfg.movePeriodMs= ToInt(ExtractValue(argc, argv, i, "--move-ms"), cfg.movePeriodMs);
        else if (StartsWith(argv[i], "--io-threads"))  cfg.ioThreads   = ToInt(ExtractValue(argc, argv, i, "--io-threads"), cfg.ioThreads);
        else if (StartsWith(argv[i], "--report-sec"))  cfg.reportSec   = ToInt(ExtractValue(argc, argv, i, "--report-sec"), cfg.reportSec);
        else if (StartsWith(argv[i], "--csv"))         cfg.csvPath     = ExtractValue(argc, argv, i, "--csv");
        else
        {
            LOG_WARN("Unknown arg: " + arg);
        }
    }

    if (!stress) return RunInteractive();

    try
    {
        StressHarness h(cfg);
        return h.Run();
    }
    catch (const std::exception& e)
    {
        LOG_ERROR(std::string("Stress fatal: ") + e.what());
        return 1;
    }
}
