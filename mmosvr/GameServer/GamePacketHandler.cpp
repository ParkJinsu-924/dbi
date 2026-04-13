#include "pch.h"
#include "GamePacketHandler.h"


SessionManager* GamePacketHandler::sSessionManager = nullptr;
PlayerService* GamePacketHandler::sPlayerService = nullptr;
MapService* GamePacketHandler::sMapService = nullptr;
std::weak_ptr<ServerSession> GamePacketHandler::sLoginSession;
std::mutex GamePacketHandler::sPendingMutex;
std::unordered_map<std::string, std::weak_ptr<GameSession>> GamePacketHandler::sPendingValidations;

void GamePacketHandler::Init(SessionManager& sessionManager, PlayerService& playerService, MapService& mapService)
{
	sSessionManager = &sessionManager;
	sPlayerService = &playerService;
	sMapService = &mapService;
}

void GamePacketHandler::SetLoginSession(std::shared_ptr<ServerSession> session)
{
	sLoginSession = session;
}

void GamePacketHandler::C_EnterGame(
	std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt)
{
	auto loginSession = sLoginSession.lock();
	if (!loginSession || !loginSession->IsConnected())
	{
		LOG_ERROR("LoginServer not connected, rejecting C_EnterGame");
		Proto::S_EnterGame response;
		response.set_success(false);
		session->Send(response);
		return;
	}

	{
		std::scoped_lock lock(sPendingMutex);
		sPendingValidations[pkt.token()] = session;
	}

	Proto::SS_ValidateToken validatePkt;
	validatePkt.set_token(pkt.token());
	loginSession->Send(validatePkt);

	LOG_INFO("Token validation requested: " + pkt.token());
}

void GamePacketHandler::SS_ValidateTokenResult(
	std::shared_ptr<ServerSession> /*session*/, const Proto::SS_ValidateTokenResult& pkt)
{
	std::shared_ptr<GameSession> gameSession;
	{
		std::scoped_lock lock(sPendingMutex);
		auto it = sPendingValidations.find(pkt.token());
		if (it == sPendingValidations.end())
		{
			LOG_ERROR("No pending validation for token: " + pkt.token());
			return;
		}
		gameSession = it->second.lock();
		sPendingValidations.erase(it);
	}

	if (!gameSession || !gameSession->IsConnected())
	{
		LOG_INFO("Client disconnected before token validation completed");
		return;
	}

	if (pkt.valid())
	{
		std::string playerName = pkt.username();
		int32 playerId = sPlayerService->AddPlayer(playerName);

		gameSession->SetPlayerId(playerId);
		gameSession->SetPlayerName(playerName);

		Proto::S_EnterGame response;
		response.set_success(true);
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

		LOG_INFO("Player entered game: id=" + std::to_string(playerId)
			+ " name=" + playerName);
	}
	else
	{
		Proto::S_EnterGame response;
		response.set_success(false);
		gameSession->Send(response);

		LOG_INFO("Token validation failed: " + pkt.token());
	}
}

void GamePacketHandler::C_PlayerMove(
	std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt)
{
	int32 playerId = session->GetPlayerId();
	if (playerId == 0)
		return;

	Proto::Vector3 validatedPos = pkt.position();

	// Validate against NavMesh
	if (sMapService && sMapService->IsLoaded())
	{
		float x = pkt.position().x();
		float y = pkt.position().y();
		float z = pkt.position().z();

		if (!sMapService->IsOnNavMesh(x, y, z))
		{
			float outX, outY, outZ;
			if (sMapService->FindNearestValidPosition(x, y, z, outX, outY, outZ))
			{
				validatedPos.set_x(outX);
				validatedPos.set_y(outY);
				validatedPos.set_z(outZ);

				// Notify the requesting client of position correction
				Proto::S_MoveCorrection correction;
				*correction.mutable_position() = validatedPos;
				session->Send(correction);
			}
			else
			{
				// No valid nearby position; ignore the move entirely
				return;
			}
		}
	}

	sPlayerService->MovePlayer(playerId, validatedPos, pkt.yaw());

	Proto::S_PlayerMove broadcast;
	broadcast.set_player_id(playerId);
	*broadcast.mutable_position() = validatedPos;
	broadcast.set_yaw(pkt.yaw());

	sSessionManager->Broadcast(session->MakeSendBuffer(broadcast));
}

void GamePacketHandler::C_Chat(
	std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt)
{
	int32 playerId = session->GetPlayerId();
	if (playerId == 0)
		return;

	Proto::S_Chat broadcast;
	broadcast.set_player_id(playerId);
	broadcast.set_sender(session->GetPlayerName());
	broadcast.set_message(pkt.message());

	sSessionManager->Broadcast(session->MakeSendBuffer(broadcast));

	LOG_INFO("[Chat] " + session->GetPlayerName() + ": " + pkt.message());
}
