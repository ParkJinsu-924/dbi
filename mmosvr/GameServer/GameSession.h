#pragma once

#include "Network/PacketSession.h"
#include "Packet/PacketHandler.h"


class GameSession : public PacketSession
{
protected:
	using PacketSession::PacketSession;

	void OnConnected() override;
	void OnDisconnected() override;
	void OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize) override;

public:
	int32 GetPlayerId() const { return playerId_; }
	void SetPlayerId(int32 id) { playerId_ = id; }

private:
	int32 playerId_ = 0;
};
