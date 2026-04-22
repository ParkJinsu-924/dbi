#include "pch.h"
#include "GameSession.h"
#include "GamePacketHandler.h"
#include "ZoneManager.h"
#include "PlayerManager.h"
#include "MapManager.h"
#include "MonsterManager.h"
#include "ResourceManager.h"
#include "MonsterTemplate.h"
#include "SpawnEntry.h"
#include "common.pb.h"
#include "Server/ServerBase.h"
#include "Server/SessionManager.h"
#include "Network/Connector.h"
#include "Network/ServerSession.h"
#include "Utils/JobQueue.h"
#include "Utils/Metrics.h"
#include "Utils/MetricsReporter.h"
#include "Packet/PacketHandler.h"


class GameServer : public ServerBase
{
public:
	GameServer(int32 port, int32 ioThreads, int32 tickRate = 100)
		: ServerBase(port, ioThreads)
		, tickRate_(tickRate)
	{
	}

	~GameServer()
	{
		StopGameLoop();
		metricsReporter_.Stop();
	}

protected:
	void Init() override
	{
		GetResourceManager().Init();
		GetMapManager().LoadNavMesh();
		GetZoneManager().Init();

		SpawnMonstersFromData();
		StartGameLoop();
		ConnectToLoginServer();

		const int tickBudgetUs = 1000000 / tickRate_;
		metricsReporter_.Start("metrics/gameserver_metrics.csv",
			std::chrono::seconds(5), tickBudgetUs);

		LOG_INFO("GameServer initialized on port " + std::to_string(port_));
	}

	SessionPtr CreateSession(tcp::socket socket, net::io_context& ioc) override
	{
		auto session = std::make_shared<GameSession>(std::move(socket), ioc);
		GetSessionManager().AddClientSession(session);
		return session;
	}

private:
	// ------ Game Loop ------
	void StartGameLoop()
	{
		GetPacketHandler().SetJobQueue(&packetQueue_);
		gameThread_ = std::jthread([this](std::stop_token st) { GameLoopThread(st); });
		LOG_INFO("GameLoop started (" + std::to_string(tickRate_) + " Hz)");
	}

	void StopGameLoop()
	{
		if (gameThread_.joinable())
		{
			gameThread_.request_stop();
			gameThread_.join();
			GetPacketHandler().SetJobQueue(nullptr);
			LOG_INFO("GameLoop stopped");
		}
	}

	void GameLoopThread(std::stop_token stopToken)
	{
		using Clock = std::chrono::steady_clock;
		const auto tickInterval = std::chrono::milliseconds(1000 / tickRate_);
		const std::uint64_t tickBudgetUs = 1000000ULL / static_cast<std::uint64_t>(tickRate_);
		auto lastTick = Clock::now();

		while (!stopToken.stop_requested())
		{
			const auto tickStart = Clock::now();
			const float dt = std::chrono::duration<float>(tickStart - lastTick).count();
			lastTick = tickStart;

			// 1. Advance game time
			GetTimeManager().Tick(dt);

			// 2. Process queued packets from I/O threads
			{
				Metrics::ScopedTimer _t(ServerMetrics::packetFlushUs);
				packetQueue_.Flush();
			}

			// 3. Update world (zones tick all GameObjects)
			{
				Metrics::ScopedTimer _t(ServerMetrics::zoneUpdateUs);
				GetZoneManager().Update(dt);
			}

			const auto tickEnd = Clock::now();
			const auto tickUs = std::chrono::duration_cast<std::chrono::microseconds>(
				tickEnd - tickStart).count();
			ServerMetrics::tickTimeUs.Observe(static_cast<std::uint64_t>(tickUs));
			if (static_cast<std::uint64_t>(tickUs) > tickBudgetUs)
				ServerMetrics::tickOverBudget.Add();

			const auto elapsed = tickEnd - tickStart;
			if (elapsed < tickInterval)
				std::this_thread::sleep_for(tickInterval - elapsed);
		}
	}

	// ------ Spawn ------
	void SpawnMonstersFromData()
	{
		const auto& spawns = GetResourceManager().Get<SpawnEntry>()->GetAll();
		if (spawns.empty())
		{
			LOG_ERROR("No spawn data found — aborting startup");
			throw std::runtime_error("SpawnEntry table is empty");
		}

		for (const auto& e : spawns | std::views::values)
		{
			Proto::Vector2 pos;
			pos.set_x(e.x); pos.set_y(e.y);
			GetMonsterManager().Spawn(e.zoneId, e.templateId, pos);
		}
	}

	// ------ Server-to-Server ------
	void ConnectToLoginServer()
	{
		auto& ioc = ioPool_.GetNextIoContext();

		Connector::Config config;
		config.endpoint = tcp::endpoint(net::ip::make_address("127.0.0.1"), 9999);
		config.interval = std::chrono::milliseconds(2000);
		config.autoReconnect = true;
		config.sessionFactory = [](tcp::socket socket, net::io_context& ioc) -> SessionPtr
			{
				return std::make_shared<ServerSession>(std::move(socket), ioc);
			};

		loginConnector_ = Connector::Create(ioc, config);

		loginConnector_->SetOnConnected([](const SessionPtr& session)
			{
				auto serverSession = std::static_pointer_cast<ServerSession>(session);
				GetSessionManager().SetServerSession(ServerType::LoginServer, serverSession);
				LOG_INFO("Connected to LoginServer");
			});

		loginConnector_->SetOnDisconnected([]()
			{
				GetSessionManager().ClearServerSession(ServerType::LoginServer);
				LOG_WARN("LoginServer disconnected, reconnecting...");
			});

		loginConnector_->Start();
	}

	// ------ Members ------
	int32 tickRate_;
	JobQueue packetQueue_;
	std::jthread gameThread_;
	std::shared_ptr<Connector> loginConnector_;
	MetricsReporter metricsReporter_;
};


int main()
{
	try
	{
		LogInit();
		GameServer server(7777, 4);
		server.Run();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::string("Fatal: ") + e.what());
		return 1;
	}
	return 0;
}
