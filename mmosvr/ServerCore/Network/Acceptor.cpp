#include "pch.h"
#include "Network/Acceptor.h"


Acceptor::Acceptor(net::io_context& acceptorIoc, tcp::endpoint endpoint,
	IoContextPool& pool, SessionFactory factory)
	: acceptor_(acceptorIoc, endpoint)
	, pool_(pool)
	, sessionFactory_(std::move(factory))
{
	acceptor_.set_option(tcp::acceptor::reuse_address(true));
}

void Acceptor::Start()
{
	DoAccept();
}

void Acceptor::Stop()
{
	boost::system::error_code ec;
	acceptor_.close(ec);
}

void Acceptor::DoAccept()
{
	auto& targetIoc = pool_.GetNextIoContext();

	acceptor_.async_accept(
		targetIoc,
		[this, &targetIoc](boost::system::error_code ec, tcp::socket socket)
			{
				if (!ec)
				{
					auto session = sessionFactory_(std::move(socket), targetIoc);
					if (session)
					{
						session->Start();
					}
				}
				else
				{
					LOG_ERROR("Accept error: " + ec.message());
				}

				if (acceptor_.is_open())
				{
					DoAccept();
				}
			}
	);
}
