#include "pch.h"
#include "GameSession.h"
#include "GamePacketHandler.h"
#include "GameLoop.h"
#include "ZoneManager.h"
#include "Services/PlayerService.h"
#include "Services/MapService.h"
#include "Server/ServerBase.h"
#include "Server/SessionManager.h"
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
		GetPlayerService().Init();
		GetMapService().Init();
		GetZoneManager().Init();

		gameLoop_ = std::make_unique<GameLoop>(100);
		gameLoop_->AddService(GetPlayerService(), 0.05f);  // 20 Hz
		gameLoop_->AddService(GetMapService(), 0.01f);      // 100 Hz
		gameLoop_->Start();

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

	std::unique_ptr<GameLoop> gameLoop_;
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
