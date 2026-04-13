#include "pch.h"
#include "GamePacketHandler.h"
#include "Packet/PacketUtils.h"


SessionManager* GamePacketHandler::sSessionManager = nullptr;
PlayerService* GamePacketHandler::sPlayerService = nullptr;
MapService* GamePacketHandler::sMapService = nullptr;
std::weak_ptr<ServerSession> GamePacketHandler::sLoginServerSession;
Synchronized<std::unordered_map<std::string, std::weak_ptr<GameSession>>, std::mutex> GamePacketHandler::sPendingValidations;

void GamePacketHandler::Init(SessionManager& sessionManager, PlayerService& playerService, MapService& mapService)
{
	sSessionManager = &sessionManager;
	sPlayerService = &playerService;
	sMapService = &mapService;
}

void GamePacketHandler::SetLoginServerSession(std::shared_ptr<ServerSession> session)
{
	sLoginServerSession = session;
}

Proto::ErrorCode GamePacketHandler::C_EnterGame(std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt)
{
	const auto loginSession = sLoginServerSession.lock();
	if (!loginSession || !loginSession->IsConnected())
	{
		LOG_ERROR("LoginServer not connected, rejecting C_EnterGame");
		return Proto::ErrorCode::LOGIN_SERVER_OFFLINE;
	}

	sPendingValidations.WithLock([&](auto& m)
		{
			m[pkt.token()] = session;
		});

	Proto::SS_ValidateToken validatePkt;
	validatePkt.set_token(pkt.token());
	loginSession->Send(validatePkt);

	LOG_INFO("Token validation requested: " + pkt.token());
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::SS_ValidateTokenResult(std::shared_ptr<ServerSession> /*session*/, const Proto::SS_ValidateTokenResult& pkt)
{
	std::shared_ptr<GameSession> gameSession;
	const bool found = sPendingValidations.WithLock([&](auto& m)
		{
			auto it = m.find(pkt.token());
			if (it == m.end())
				return false;
			gameSession = it->second.lock();
			m.erase(it);
			return true;
		});

	if (!found)
	{
		// Stale validation result (e.g. client disconnected). Not a server bug — log only.
		LOG_ERROR("No pending validation for token: " + pkt.token());
		return Proto::ErrorCode::OK;
	}

	if (!gameSession || !gameSession->IsConnected())
	{
		LOG_INFO("Client disconnected before token validation completed");
		return Proto::ErrorCode::OK;
	}

	if (!pkt.valid())
	{
		// Token rejected by LoginServer — surface as S_Error to the original GameSession
		// (not to the ServerSession this handler runs on).
		Proto::S_Error errPkt;
		errPkt.set_source_packet_id(static_cast<uint32>(PacketId::C_ENTER_GAME));
		errPkt.set_code(Proto::ErrorCode::TOKEN_INVALID);
		gameSession->Send(errPkt);
		LOG_INFO("Token validation failed: " + pkt.token());
		return Proto::ErrorCode::OK;
	}

	const std::string playerName = pkt.username();
	const int32 playerId = sPlayerService->AddPlayer(playerName);

	gameSession->SetPlayerId(playerId);
	gameSession->SetPlayerName(playerName);

	Proto::S_EnterGame response;
	response.set_player_id(playerId);
	auto* spawnPos = response.mutable_spawn_position();
	spawnPos->set_x(0.0f);
	spawnPos->set_y(0.0f);
	spawnPos->set_z(0.0f);
	gameSession->Send(response);

	auto allPlayers = sPlayerService->GetAllPlayers();
	Proto::S_PlayerList playerListPkt;
	for (const auto& p : allPlayers)
	{
		auto* info = playerListPkt.add_players();
		info->set_player_id(p.playerId);
		info->set_name(p.name);
		*info->mutable_position() = p.position;
	}
	gameSession->Send(playerListPkt);

	LOG_INFO("Player entered game: id=" + std::to_string(playerId) + " name=" + playerName);
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt)
{
	const int32 playerId = session->GetPlayerId();
	if (playerId == 0)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	Proto::Vector3 validatedPos = pkt.position();

	// Validate against NavMesh
	if (sMapService && sMapService->IsLoaded())
	{
		const float x = pkt.position().x();
		const float y = pkt.position().y();
		const float z = pkt.position().z();

		if (!sMapService->IsOnNavMesh(x, y, z))
		{
			float outX, outY, outZ;
			if (sMapService->FindNearestValidPosition(x, y, z, outX, outY, outZ))
			{
				validatedPos.set_x(outX);
				validatedPos.set_y(outY);
				validatedPos.set_z(outZ);

				// Notify the requesting client of position correction (side-channel packet,
				// not a request response — sent manually).
				Proto::S_MoveCorrection correction;
				*correction.mutable_position() = validatedPos;
				session->Send(correction);
			}
			else
			{
				// No valid nearby position; reject the move entirely.
				return Proto::ErrorCode::INVALID_POSITION;
			}
		}
	}

	sPlayerService->MovePlayer(playerId, validatedPos, pkt.yaw());

	Proto::S_PlayerMove broadcast;
	broadcast.set_player_id(playerId);
	*broadcast.mutable_position() = validatedPos;
	broadcast.set_yaw(pkt.yaw());

	sSessionManager->Broadcast(session->MakeSendBuffer(broadcast));
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt)
{
	const int32 playerId = session->GetPlayerId();
	if (playerId == 0)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	Proto::S_Chat broadcast;
	broadcast.set_player_id(playerId);
	broadcast.set_sender(session->GetPlayerName());
	broadcast.set_message(pkt.message());

	sSessionManager->Broadcast(session->MakeSendBuffer(broadcast));

	LOG_INFO("[Chat] " + session->GetPlayerName() + ": " + pkt.message());
	return Proto::ErrorCode::OK;
}
