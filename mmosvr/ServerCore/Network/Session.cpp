#include "pch.h"
#include "Network/Session.h"


Session::Session(tcp::socket socket, net::io_context& ioc)
	: ioc_(ioc)
	, socket_(std::move(socket))
{
}

Session::~Session()
{
	Disconnect();
}

void Session::Start()
{
	connected_ = true;
	OnConnected();
	DoRead();
}

void Session::Disconnect()
{
	bool expected = true;
	if (!connected_.compare_exchange_strong(expected, false))
		return;

	boost::system::error_code ec;
	socket_.shutdown(tcp::socket::shutdown_both, ec);
	socket_.close(ec);

	OnDisconnected();
}

bool Session::IsConnected() const
{
	return connected_;
}

void Session::Send(SendBufferChunkPtr chunk)
{
	if (!connected_)
		return;

	sendBuffer_.Push(std::move(chunk));

	bool expected = false;
	if (writeInProgress_.compare_exchange_strong(expected, true))
	{
		DoWrite();
	}
}

void Session::DoRead()
{
	if (!connected_)
		return;

	recvBuffer_.Compact();

	if (recvBuffer_.FreeSize() == 0)
	{
		LOG_ERROR("RecvBuffer overflow, disconnecting");
		Disconnect();
		return;
	}

	auto self = shared_from_this();
	socket_.async_read_some(
		net::buffer(recvBuffer_.WritePos(), recvBuffer_.FreeSize()),
		[self](boost::system::error_code ec, std::size_t bytesTransferred)
			{
				if (ec)
				{
					self->Disconnect();
					return;
				}

				self->recvBuffer_.OnWrite(static_cast<int32>(bytesTransferred));

				int32 consumed = self->OnRecv(
					self->recvBuffer_.ReadPos(),
					self->recvBuffer_.DataSize());

				if (consumed < 0)
				{
					self->Disconnect();
					return;
				}

				self->recvBuffer_.OnRead(consumed);
				self->DoRead();
			}
	);
}

void Session::DoWrite()
{
	auto chunks = sendBuffer_.PopAll();
	if (chunks.empty())
	{
		writeInProgress_ = false;
		// Double-check for race condition
		if (!sendBuffer_.Empty())
		{
			bool expected = false;
			if (writeInProgress_.compare_exchange_strong(expected, true))
			{
				DoWrite();
			}
		}
		return;
	}

	std::vector<net::const_buffer> buffers;
	buffers.reserve(chunks.size());
	for (auto& c : chunks)
	{
		buffers.emplace_back(c->Buffer(), c->Size());
	}

	auto self = shared_from_this();
	net::async_write(socket_, buffers,
		[self, chunks = std::move(chunks)](boost::system::error_code ec, std::size_t)
			{
				if (ec)
				{
					self->Disconnect();
					return;
				}
				self->DoWrite();
			}
	);
}
