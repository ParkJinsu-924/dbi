#include "pch.h"
#include "Network/Connector.h"


Connector::Connector(net::io_context& ioc, SessionFactory factory)
	: ioc_(ioc)
	, sessionFactory_(std::move(factory))
{
}

void Connector::Connect(const tcp::endpoint& endpoint)
{
	auto socket = std::make_shared<tcp::socket>(ioc_);

	socket->async_connect(endpoint,
		[this, socket](boost::system::error_code ec)
			{
				if (ec)
				{
					LOG_ERROR("Connect failed: " + ec.message());
					return;
				}

				auto session = sessionFactory_(std::move(*socket), ioc_);
				if (session)
				{
					session->Start();
				}
			}
	);
}
