#include "pch.h"
#include "GameSession.h"
#include "PlayerManager.h"
#include "Server/SessionManager.h"
#include "ZoneManager.h"
#include "Packet/PacketHandler.h"
#include "PacketMaker.h"
#include "game.pb.h"


void GameSession::OnConnected()
{
	LOG_INFO("GameSession connected");
}

void GameSession::OnDisconnected()
{
	LOG_INFO("GameSession disconnected: playerId=" + std::to_string(playerId_));

	const int32 playerId = playerId_;
	if (playerId != 0)
	{
		// Zone / PlayerManager 접근은 반드시 GameLoopThread 에서 일어나도록 JobQueue 로 post.
		// (OnDisconnected 는 I/O 스레드의 소켓 콜백 컨텍스트라 직접 Zone 을 건드리면 race)
		auto* jobQueue = GetPacketHandler().GetJobQueue();
		if (jobQueue)
		{
			jobQueue->Push([playerId]()
			{
				auto& playerService = GetPlayerManager();
				Zone* zone = nullptr;
				long long guid = 0;

				if (auto player = playerService.FindPlayer(playerId))
				{
					zone = &player->GetZone();
					guid = player->GetGuid();
					player->UnbindSession();
				}

				if (zone) zone->Remove(guid);

				playerService.RemovePlayer(playerId);

				if (zone) zone->Broadcast(PacketMaker::MakePlayerLeave(playerId));
			});
		}
	}

	GetSessionManager().RemoveClientSession(shared_from_this());
}

void GameSession::OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize)
{
	GetPacketHandler().Dispatch(
		std::static_pointer_cast<PacketSession>(shared_from_this()),
		packetId, payload, payloadSize);
}
