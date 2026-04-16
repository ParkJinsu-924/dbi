#include "pch.h"
#include "LoginSession.h"


void LoginSession::OnConnected()
{
	LOG_INFO("LoginSession connected");
}

void LoginSession::OnDisconnected()
{
	LOG_INFO("LoginSession disconnected");
}

void LoginSession::OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize)
{
	GetPacketHandler().Dispatch(
		std::static_pointer_cast<PacketSession>(shared_from_this()),
		packetId, payload, payloadSize);
}
