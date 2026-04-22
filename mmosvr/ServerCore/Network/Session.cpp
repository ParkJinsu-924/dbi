#include "pch.h"
#include "Network/Session.h"
#include "Utils/Metrics.h"


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

	if (disconnectCallback_)
		disconnectCallback_();
}

bool Session::IsConnected() const
{
	return connected_;
}

void Session::SetDisconnectCallback(std::function<void()> cb)
{
	disconnectCallback_ = std::move(cb);
}

void Session::Send(SendBufferChunkPtr chunk)
{
	if (!connected_)
		return;

	ServerMetrics::sendCalls.Add();

	// 호출 스레드와 무관하게 실제 송신은 자기 io_context 에서 수행.
	// GameLoop 의 Broadcast fan-out(N 세션) 이 I/O 스레드들에 분산되어 tick 시간이 짧아진다.
	// 한 세션은 하나의 io_context 에 바인딩 → 같은 세션에 대한 복수 Send 는 순차 실행 보장(strand 불필요).
	auto self = shared_from_this();
	net::post(ioc_, [self, chunk = std::move(chunk)]() mutable
		{
			if (!self->connected_) return;

			self->sendBuffer_.Push(std::move(chunk));

			bool expected = false;
			if (self->writeInProgress_.compare_exchange_strong(expected, true))
			{
				self->DoWrite();
			}
		});
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

				ServerMetrics::bytesRecv.Add(bytesTransferred);

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
	std::uint64_t totalBytes = 0;
	for (auto& c : chunks)
	{
		buffers.emplace_back(c->Buffer(), c->Size());
		totalBytes += static_cast<std::uint64_t>(c->Size());
	}
	const std::uint64_t chunkCount = chunks.size();

	auto self = shared_from_this();
	net::async_write(socket_, buffers,
		[self, chunks = std::move(chunks), chunkCount, totalBytes]
			(boost::system::error_code ec, std::size_t)
			{
				if (ec)
				{
					self->Disconnect();
					return;
				}
				ServerMetrics::packetsSent.Add(chunkCount);
				ServerMetrics::bytesSent.Add(totalBytes);
				self->DoWrite();
			}
	);
}
