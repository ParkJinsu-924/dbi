#include "pch.h"
#include "GameSession.h"
#include "GamePacketHandler.h"
#include "ZoneManager.h"
#include "Services/PlayerService.h"
#include "Services/MapService.h"
#include "Services/MonsterService.h"
#include "common.pb.h"
#include "Server/ServerBase.h"
#include "Server/SessionManager.h"
#include "Network/Connector.h"
#include "Network/ServerSession.h"
#include "Utils/JobQueue.h"
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
	}

protected:
	void Init() override
	{
		GetPlayerService().Init();
		GetMapService().Init();
		GetZoneManager().Init();

		SpawnTestMonsters();
		StartGameLoop();
		ConnectToLoginServer();

		LOG_INFO("GameServer initialized on port " + std::to_string(port_));
	}

	SessionPtr CreateSession(tcp::socket socket, net::io_context& ioc) override
	{
		auto session = std::make_shared<GameSession>(std::move(socket), ioc);
		GetSessionManager().AddGameSession(session);
		return session;
	}

private:
	// ------ Game Loop ------
	void StartGameLoop()
	{
		GetPacketHandler().SetJobQueue(&jobQueue_);
		gameThread_ = std::jthread([this](std::stop_token st) { RunGameLoop(st); });
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

	void RunGameLoop(std::stop_token stopToken)
	{
		using Clock = std::chrono::steady_clock;
		const auto tickInterval = std::chrono::milliseconds(1000 / tickRate_);
		auto lastTick = Clock::now();

		while (!stopToken.stop_requested())
		{
			const auto now = Clock::now();
			const float dt = std::chrono::duration<float>(now - lastTick).count();
			lastTick = now;

			// 1. Process queued packets from I/O threads
			jobQueue_.Flush();

			// 2. Update world (zones tick all GameObjects)
			GetZoneManager().Update(dt);

			const auto elapsed = Clock::now() - now;
			if (elapsed < tickInterval)
				std::this_thread::sleep_for(tickInterval - elapsed);
		}
	}

	// ------ Test ------
	void SpawnTestMonsters()
	{
		auto makeVec = [](float x, float y, float z) {
			Proto::Vector3 v;
			v.set_x(x); v.set_y(y); v.set_z(z);
			return v;
		};

		GetMonsterService().Spawn(1, "Goblin",
			makeVec(5.0f, 0.0f, 0.0f),   /*radius*/3.0f, /*angularSpeed*/0.8f, /*start*/0.0f);

		GetMonsterService().Spawn(1, "Orc",
			makeVec(-5.0f, 0.0f, 3.0f),  /*radius*/2.5f, /*angularSpeed*/-1.2f, /*start*/1.0f);

		GetMonsterService().Spawn(1, "Slime",
			makeVec(0.0f, 0.0f, -6.0f),  /*radius*/4.0f, /*angularSpeed*/0.5f, /*start*/2.0f);
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
	JobQueue jobQueue_;
	std::jthread gameThread_;
	std::shared_ptr<Connector> loginConnector_;
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
