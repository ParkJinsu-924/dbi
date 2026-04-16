#include "pch.h"
#include "GameSession.h"
#include "Services/PlayerService.h"
#include "Server/SessionManager.h"
#include "ZoneManager.h"
#include "game.pb.h"


void GameSession::OnConnected()
{
	LOG_INFO("GameSession connected");
}

void GameSession::OnDisconnected()
{
	LOG_INFO("GameSession disconnected: playerId=" + std::to_string(playerId_));

	if (playerId_ != 0)
	{
		auto& playerService = GetPlayerService();
		int32 zoneId = 0;
		long long guid = 0;

		if (auto player = playerService.FindPlayer(playerId_))
		{
			zoneId = player->GetZoneId();
			guid = player->GetGuid();
			player->UnbindSession();
		}

		// Remove from zone first so the leave broadcast doesn't target this player
		if (auto* zone = GetZoneManager().GetZone(zoneId))
			zone->Remove(guid);

		playerService.RemovePlayer(playerId_);

		Proto::S_PlayerLeave leavePkt;
		leavePkt.set_player_id(playerId_);
		if (auto* zone = GetZoneManager().GetZone(zoneId))
			zone->Broadcast(MakeSendBuffer(leavePkt));
	}

	GetSessionManager().RemoveGameSession(shared_from_this());
}

void GameSession::OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize)
{
	GetPacketHandler().Dispatch(
		std::static_pointer_cast<PacketSession>(shared_from_this()),
		packetId, payload, payloadSize);
}
