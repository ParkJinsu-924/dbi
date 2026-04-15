#pragma once

#include "Network/RecvBuffer.h"
#include "Network/SendBuffer.h"


class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(tcp::socket socket, net::io_context& ioc);
	virtual ~Session();

	void Start();
	void Disconnect();
	bool IsConnected() const;

	void Send(SendBufferChunkPtr chunk);
	void SetDisconnectCallback(std::function<void()> cb);

	tcp::socket& GetSocket() { return socket_; }

protected:
	virtual void OnConnected()
	{
	}
	virtual void OnDisconnected()
	{
	}
	virtual int32 OnRecv(char* buffer, int32 len) = 0;

	net::io_context& ioc_;

private:
	void DoRead();
	void DoWrite();

	tcp::socket socket_;
	RecvBuffer recvBuffer_;
	SendBuffer sendBuffer_;
	std::atomic<bool> connected_{false};
	std::atomic<bool> writeInProgress_{false};
	std::function<void()> disconnectCallback_;
};

using SessionPtr = std::shared_ptr<Session>;
