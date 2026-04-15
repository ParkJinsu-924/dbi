#include "pch.h"
#include "GameSession.h"
#include "Services/PlayerService.h"
#include "Server/SessionManager.h"
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
		if (auto player = playerService.FindPlayer(playerId_))
		{
			player->UnbindSession();
		}

		playerService.RemovePlayer(playerId_);

		Proto::S_PlayerLeave leavePkt;
		leavePkt.set_player_id(playerId_);
		GetSessionManager().BroadcastToGameSessions(MakeSendBuffer(leavePkt));
	}

	GetSessionManager().RemoveGameSession(shared_from_this());
}

void GameSession::OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize)
{
	PacketHandler::Instance().Dispatch(
		std::static_pointer_cast<PacketSession>(shared_from_this()),
		packetId, payload, payloadSize);
}
