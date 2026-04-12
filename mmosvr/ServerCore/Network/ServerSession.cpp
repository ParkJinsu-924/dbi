#include "pch.h"
#include "Network/ServerSession.h"


void ServerSession::OnConnected()
{
	LOG_INFO("ServerSession connected");
}

void ServerSession::OnDisconnected()
{
	LOG_INFO("ServerSession disconnected");
}

void ServerSession::OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize)
{
	PacketHandler::Instance().Dispatch(
		std::static_pointer_cast<PacketSession>(shared_from_this()),
		packetId, payload, payloadSize);
}
