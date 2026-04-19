#include "pch.h"
#include "Network/PacketSession.h"
#include "Packet/PacketUtils.h"
#include "login.pb.h"
#include "game.pb.h"


class DummyClientSession : public PacketSession
{
public:
	using PacketSession::PacketSession;

	void OnConnected() override
	{
		LOG_INFO("Connected to server!");
	}

	void OnDisconnected() override
	{
		LOG_INFO("Disconnected from server.");
	}

	void OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize) override
	{
		switch (static_cast<PacketId>(packetId))
		{
		case PacketId::S_LOGIN:
		{
			Proto::S_Login pkt;
			pkt.ParseFromArray(payload, payloadSize);
			LOG_INFO("Login response: token=" + pkt.token()
				+ " gameServer=" + pkt.game_server_ip()
				+ ":" + std::to_string(pkt.game_server_port()));
			break;
		}
		case PacketId::S_ENTER_GAME:
		{
			Proto::S_EnterGame pkt;
			pkt.ParseFromArray(payload, payloadSize);
			LOG_INFO("Enter game: playerId=" + std::to_string(pkt.player_id()));
			break;
		}
		case PacketId::S_ERROR:
		{
			Proto::S_Error pkt;
			pkt.ParseFromArray(payload, payloadSize);
			LOG_ERROR("Server error: source_packet_id=" + std::to_string(pkt.source_packet_id())
				+ " code=" + std::to_string(static_cast<int>(pkt.code()))
				+ " detail=" + pkt.detail());
			break;
		}
		case PacketId::S_PLAYER_MOVE:
		{
			Proto::S_PlayerMove pkt;
			pkt.ParseFromArray(payload, payloadSize);
			LOG_INFO("Player moved: id=" + std::to_string(pkt.player_id())
				+ " pos=(" + std::to_string(pkt.position().x())
				+ "," + std::to_string(pkt.position().y()) + ")");
			break;
		}
		case PacketId::S_CHAT:
		{
			Proto::S_Chat pkt;
			pkt.ParseFromArray(payload, payloadSize);
			LOG_INFO("[Chat] " + pkt.sender() + ": " + pkt.message());
			break;
		}
		default:
			LOG_WARN("Unknown packet id=" + std::to_string(packetId));
			break;
		}
	}
};


int main()
{
	LogInit();

	try
	{
		net::io_context ioc;

		// Connect to LoginServer
		tcp::resolver resolver(ioc);
		auto endpoints = resolver.resolve("127.0.0.1", "9999");

		tcp::socket socket(ioc);
		net::connect(socket, endpoints);

		auto session = std::make_shared<DummyClientSession>(std::move(socket), ioc);
		session->Start();

		// Send login request
		Proto::C_Login loginPkt;
		loginPkt.set_username("testuser");
		loginPkt.set_password("testpass");
		session->Send(loginPkt);

		LOG_INFO("Login request sent. Running event loop...");

		// Run the event loop (blocks until disconnected)
		ioc.run();
	}
	catch (const std::exception& e)
	{
		LOG_ERROR(std::string("Error: ") + e.what());
		return 1;
	}

	return 0;
}
