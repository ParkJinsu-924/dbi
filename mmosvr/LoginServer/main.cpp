#include "pch.h"
#include "LoginSession.h"
#include "LoginPacketHandler.h"
#include "Server/ServerBase.h"


class LoginServer : public ServerBase
{
public:
	LoginServer(int32 port, int32 ioThreads)
		: ServerBase(port, ioThreads)
	{
	}

protected:
	void Init() override
	{
		LOG_INFO("LoginServer initialized on port " + std::to_string(port_));
	}

	SessionPtr CreateSession(tcp::socket socket, net::io_context& ioc) override
	{
		auto session = std::make_shared<LoginSession>(std::move(socket), ioc);
		GetSessionManager().AddGameSession(session);
		return session;
	}

};


int main()
{
	try
	{
		LogInit();
		LoginServer server(9999, 2);
		server.Run();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::string("Fatal: ") + e.what());
		return 1;
	}
	return 0;
}
