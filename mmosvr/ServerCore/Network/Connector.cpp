#include "pch.h"
#include "Network/Connector.h"


Connector::Connector(
	net::io_context& ioc,
	const Config& config)
	: ioc_(ioc)
	, config_(config)
	, timer_(ioc)
{
}

std::shared_ptr<Connector> Connector::Create(
	net::io_context& ioc,
	const Config& config)
{
	return std::shared_ptr<Connector>(
		new Connector(ioc, config));
}

void Connector::Start()
{
	running_ = true;
	DoConnect();
}

void Connector::Stop()
{
	running_ = false;
	net::post(ioc_, [self = shared_from_this()]()
		{
			self->timer_.cancel();
		});
}

void Connector::SetOnConnected(ConnectedCallback cb)
{
	onConnected_ = std::move(cb);
}

void Connector::SetOnDisconnected(DisconnectedCallback cb)
{
	onDisconnected_ = std::move(cb);
}

void Connector::DoConnect()
{
	if (!running_)
		return;

	auto socket = std::make_shared<tcp::socket>(ioc_);

	socket->async_connect(config_.endpoint,
		[self = shared_from_this(), socket](boost::system::error_code ec)
			{
				if (!self->running_)
					return;

				if (ec)
				{
					LOG_ERROR("Connect failed: " + ec.message());
					if (self->config_.autoReconnect)
						self->ScheduleReconnect();
					return;
				}

				const auto session = self->config_.sessionFactory(std::move(*socket), self->ioc_);
				if (!session)
					return;

				if (self->config_.autoReconnect)
				{
					std::weak_ptr<Connector> weakSelf = self;
					session->SetDisconnectCallback([weakSelf, &ioc = self->ioc_]()
						{
							net::post(ioc, [weakSelf]()
								{
									if (const auto self = weakSelf.lock())
										self->OnSessionDisconnected();
								});
						});
				}

				session->Start();
				self->activeSession_ = session;

				if (self->onConnected_)
					self->onConnected_(session);
			});
}

void Connector::ScheduleReconnect()
{
	if (!running_)
		return;

	LOG_INFO("Reconnecting in " + std::to_string(config_.interval.count()) + "ms...");
	
	timer_.expires_after(config_.interval);
	timer_.async_wait(
		[self = shared_from_this()](const boost::system::error_code& ec)
			{
				if (ec || !self->running_)
					return;

				self->DoConnect();
			});
}

void Connector::OnSessionDisconnected()
{
	if (!running_)
		return;

	if (onDisconnected_)
		onDisconnected_();

	ScheduleReconnect();
}
