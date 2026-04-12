#pragma once

#include "Network/Session.h"


class Connector
{
public:
	using SessionFactory = std::function<SessionPtr(tcp::socket, net::io_context&)>;

	Connector(net::io_context& ioc, SessionFactory factory);

	void Connect(const tcp::endpoint& endpoint);

private:
	net::io_context& ioc_;
	SessionFactory sessionFactory_;
};
