#include "pch.h"
#include "GameSession.h"
#include "GamePacketHandler.h"
#include "GameLoop.h"
#include "Services/PlayerService.h"
#include "Services/MapService.h"
#include "Server/ServerBase.h"
#include "Network/Connector.h"
#include "Network/ServerSession.h"


class GameServer : public ServerBase
{
public:
	GameServer(int32 port, int32 ioThreads)
		: ServerBase(port, ioThreads)
	{
	}

protected:
	void Init() override
	{
		auto playerService = std::make_shared<PlayerService>();
		GetServiceLocator().Register<PlayerService>(playerService);
		playerService->Init();

		auto mapService = std::make_shared<MapService>();
		GetServiceLocator().Register<MapService>(mapService);
		mapService->Init();

		GamePacketHandler::Init(GetSessionManager(), *playerService, *mapService);

		GameSession::SetServices(&GetSessionManager(), playerService.get());

		gameLoop_ = std::make_unique<GameLoop>(100);
		gameLoop_->AddService(playerService, 0.05f);  // 20 Hz
		gameLoop_->AddService(mapService, 0.01f);      // 100 Hz
		gameLoop_->Start();

		ConnectToLoginServer();

		LOG_INFO("GameServer initialized on port " + std::to_string(port_));
	}

	SessionPtr CreateSession(tcp::socket socket, net::io_context& ioc) override
	{
		auto session = std::make_shared<GameSession>(std::move(socket), ioc);
		GetSessionManager().Add(session);
		return session;
	}

private:
	void ConnectToLoginServer()
	{
		auto& ioc = ioPool_.GetNextIoContext();
		connector_ = std::make_unique<Connector>(ioc,
			[this](tcp::socket socket, net::io_context& ioc) -> SessionPtr
			{
				auto session = std::make_shared<ServerSession>(std::move(socket), ioc);
				loginSession_ = session;
				GamePacketHandler::SetLoginServerSession(session);
				LOG_INFO("Connected to LoginServer");
				return session;
			});

		auto endpoint = tcp::endpoint(
			net::ip::make_address("127.0.0.1"), 9999);
		connector_->Connect(endpoint);
	}

	std::unique_ptr<GameLoop> gameLoop_;
	std::unique_ptr<Connector> connector_;
	std::shared_ptr<ServerSession> loginSession_;
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
