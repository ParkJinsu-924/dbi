#include "pch.h"
#include "Server/ServerBase.h"


ServerBase::ServerBase(int32 port, int32 ioThreadCount)
	: port_(port)
	, ioPool_(ioThreadCount)
{
	acceptorWorkGuard_ = std::make_unique<net::executor_work_guard<net::io_context::executor_type>>(
		acceptorIoc_.get_executor());
}

ServerBase::~ServerBase()
{
	Stop();
}

void ServerBase::Run()
{
	Init();

	tcp::endpoint endpoint(tcp::v4(), static_cast<uint16>(port_));

	acceptor_ = std::make_unique<Acceptor>(
		acceptorIoc_, endpoint, ioPool_,
		[this](tcp::socket socket, net::io_context& ioc) -> SessionPtr
			{
				return CreateSession(std::move(socket), ioc);
			}
	);

	acceptor_->Start();
	LOG_INFO("Server listening on port " + std::to_string(port_));

	// Start I/O threads
	ioPool_.Run();

	// Run acceptor on main thread (blocks)
	acceptorIoc_.run();
}

void ServerBase::Stop()
{
	LOG_INFO("Server shutting down...");

	if (acceptor_)
		acceptor_->Stop();

	GetSessionManager().ClearClientSessions();
	ioPool_.Stop();

	acceptorWorkGuard_.reset();
	acceptorIoc_.stop();
}
