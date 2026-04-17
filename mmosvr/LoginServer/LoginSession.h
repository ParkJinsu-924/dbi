#pragma once

#include "Network/PacketSession.h"
#include "Packet/PacketHandler.h"


class LoginSession : public PacketSession
{
protected:
	using PacketSession::PacketSession;

	void OnConnected() override;
	void OnDisconnected() override;
	void OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize) override;
};
