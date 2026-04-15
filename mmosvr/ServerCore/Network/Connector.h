#pragma once

#include "Network/Session.h"


class Connector : public std::enable_shared_from_this<Connector>
{
public:
	using SessionFactory = std::function<SessionPtr(tcp::socket, net::io_context&)>;
	using ConnectedCallback = std::function<void(SessionPtr)>;
	using DisconnectedCallback = std::function<void()>;

	struct Config
	{
		tcp::endpoint endpoint;
		std::chrono::milliseconds interval;
		bool autoReconnect{true};
		SessionFactory sessionFactory;
	};

	static std::shared_ptr<Connector> Create(
		net::io_context& ioc,
		const Config& config = {});

	void Start();
	void Stop();

	void SetOnConnected(ConnectedCallback cb);
	void SetOnDisconnected(DisconnectedCallback cb);

private:
	Connector(net::io_context& ioc, const Config& config);

	void DoConnect();
	void ScheduleReconnect();
	void OnSessionDisconnected();

	net::io_context& ioc_;
	Config config_;
	net::steady_timer timer_;

	std::atomic<bool> running_{false};
	std::weak_ptr<Session> activeSession_;

	ConnectedCallback onConnected_;
	DisconnectedCallback onDisconnected_;
};
