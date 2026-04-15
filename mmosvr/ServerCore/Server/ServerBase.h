#pragma once

#include "Network/IoContextPool.h"
#include "Network/Acceptor.h"
#include "Server/SessionManager.h"


class ServerBase
{
public:
	ServerBase(int32 port, int32 ioThreadCount);
	virtual ~ServerBase();

	void Run();
	void Stop();

protected:
	virtual void Init() = 0;
	virtual SessionPtr CreateSession(tcp::socket socket, net::io_context& ioc) = 0;

	int32 port_;
	net::io_context acceptorIoc_;
	IoContextPool ioPool_;
	std::unique_ptr<Acceptor> acceptor_;

private:
	std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> acceptorWorkGuard_;
};
