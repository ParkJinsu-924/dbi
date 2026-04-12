#pragma once

#include "Network/IoContextPool.h"
#include "Network/Session.h"


class Acceptor
{
public:
	using SessionFactory = std::function<SessionPtr(tcp::socket, net::io_context&)>;

	Acceptor(net::io_context& acceptorIoc, tcp::endpoint endpoint,
		IoContextPool& pool, SessionFactory factory);

	void Start();
	void Stop();

private:
	void DoAccept();

	tcp::acceptor acceptor_;
	IoContextPool& pool_;
	SessionFactory sessionFactory_;
};
