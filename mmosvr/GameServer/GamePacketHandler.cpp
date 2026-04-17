#include "pch.h"
#include "GamePacketHandler.h"
#include "Packet/PacketUtils.h"
#include "Packet/PacketHandler.h"
#include "Server/SessionManager.h"
#include "PlayerManager.h"
#include "MapManager.h"
#include "Monster.h"
#include "ZoneManager.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include <cmath>


Synchronized<std::unordered_map<std::string, std::weak_ptr<GameSession>>, std::mutex> GamePacketHandler::sPendingValidations;

Proto::ErrorCode GamePacketHandler::C_EnterGame(std::shared_ptr<GameSession> session, const Proto::C_EnterGame& pkt)
{
	const auto loginSession = GetSessionManager().GetServerSession(ServerType::LoginServer);
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

Proto::ErrorCode GamePacketHandler::SS_ValidateToken(std::shared_ptr<ServerSession> /*session*/, const Proto::SS_ValidateTokenResult& pkt)
{
	std::shared_ptr<GameSession> gameSession;
	sPendingValidations.WithLock([&](auto& m)
		{
			auto it = m.find(pkt.token());
			if (it == m.end())
				return;
		
			gameSession = it->second.lock();
			m.erase(it);
		});

	if (!gameSession)
	{
		LOG_ERROR("No pending validation for token: " + pkt.token());
		return Proto::ErrorCode::OK;
	}

	if (!gameSession->IsConnected())
	{
		LOG_INFO("Client disconnected before token validation completed");
		return Proto::ErrorCode::OK;
	}

	if (!pkt.valid())
	{ // In this time, LoginServer <-> GameServer
		SendErrorTo<Proto::C_EnterGame>(gameSession, Proto::ErrorCode::TOKEN_INVALID);
		LOG_INFO("Token validation failed: " + pkt.token());
		return Proto::ErrorCode::OK;
	}
	
	const std::string playerName = pkt.username();
	const auto player = GetPlayerManager().AddPlayer(playerName);
	const int32 playerId = player->GetPlayerId();

	gameSession->SetPlayerId(playerId);
	player->BindSession(gameSession);

	// Put the player into the default zone
	player->SetZoneId(DEFAULT_ZONE_ID);
	if (auto* zone = GetZoneManager().GetZone(DEFAULT_ZONE_ID))
		zone->Add(player);

	Proto::S_EnterGame response;
	response.set_player_id(playerId);
	auto* spawnPos = response.mutable_spawn_position();
	spawnPos->set_x(0.0f);
	spawnPos->set_y(0.0f);
	spawnPos->set_z(0.0f);
	gameSession->Send(response);

	auto allPlayers = GetPlayerManager().GetAllPlayers();
	Proto::S_PlayerList playerListPkt;
	for (const auto& p : allPlayers)
	{
		auto* info = playerListPkt.add_players();
		info->set_player_id(p->GetPlayerId());
		info->set_name(p->GetName());
		*info->mutable_position() = p->GetPosition();
	}
	gameSession->Send(playerListPkt);

	// Send existing monsters in the zone
	auto* defaultZone = GetZoneManager().GetZone(DEFAULT_ZONE_ID);
	auto monstersInZone = defaultZone ? defaultZone->GetObjectsByType<Monster>()
		: std::vector<std::shared_ptr<Monster>>{};
	Proto::S_MonsterList monsterListPkt;
	for (const auto& m : monstersInZone)
	{
		auto* info = monsterListPkt.add_monsters();
		info->set_guid(m->GetGuid());
		info->set_name(m->GetName());
		*info->mutable_position() = m->GetPosition();
		info->set_detect_range(m->GetDetectRange());
	}
	gameSession->Send(monsterListPkt);

	LOG_INFO("Player entered game: id=" + std::to_string(playerId) + " name=" + playerName);
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	Proto::Vector3 validatedPos = pkt.position();

	auto& mapService = GetMapManager();
	if (mapService.IsLoaded())
	{
		const float x = pkt.position().x();
		const float y = pkt.position().y();
		const float z = pkt.position().z();

		if (!mapService.IsOnNavMesh(x, y, z))
		{
			float outX, outY, outZ;
			if (mapService.FindNearestValidPosition(x, y, z, outX, outY, outZ))
			{
				validatedPos.set_x(outX);
				validatedPos.set_y(outY);
				validatedPos.set_z(outZ);

				Proto::S_MoveCorrection correction;
				*correction.mutable_position() = validatedPos;
				session->Send(correction);
			}
			else
			{
				return Proto::ErrorCode::INVALID_POSITION;
			}
		}
	}

	player->SetPosition(validatedPos);
	player->SetYaw(pkt.yaw());

	Proto::S_PlayerMove broadcast;
	broadcast.set_player_id(player->GetPlayerId());
	*broadcast.mutable_position() = validatedPos;
	broadcast.set_yaw(pkt.yaw());

	if (auto* zone = GetZoneManager().GetZone(player->GetZoneId()))
		zone->Broadcast(broadcast);
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	Proto::S_Chat broadcast;
	broadcast.set_player_id(player->GetPlayerId());
	broadcast.set_sender(player->GetName());
	broadcast.set_message(pkt.message());

	if (auto* zone = GetZoneManager().GetZone(player->GetZoneId()))
		zone->Broadcast(broadcast);

	LOG_INFO("[Chat] " + player->GetName() + ": " + pkt.message());
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_RequestUseSkill(std::shared_ptr<GameSession> session, const Proto::C_RequestUseSkill& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	auto* zone = GetZoneManager().GetZone(player->GetZoneId());
	if (!zone)
		return Proto::ErrorCode::INTERNAL_ERROR;

	const auto* skTable = GetResourceManager().Get<SkillTemplate>();
	const SkillTemplate* sk = skTable ? skTable->FindByName(pkt.skill_name()) : nullptr;
	if (!sk)
	{
		LOG_WARN("C_RequestUseSkill: unknown skill name '" + pkt.skill_name() + "'");
		return Proto::ErrorCode::INVALID_REQUEST;
	}

	if (!player->TryConsumeCooldown(sk->name, sk->cooldown))
		return Proto::ErrorCode::OK;  // 쿨다운 중 — 조용히 무시

	if (sk->kind == 0)  // Homing
	{
		std::shared_ptr<Monster> target;
		if (pkt.target_guid() != 0)
		{
			target = zone->FindAs<Monster>(pkt.target_guid());
		}
		else
		{
			target = zone->FindNearestMonster(player->GetPosition(), 30.0f);
		}

		if (!target || !target->IsAlive())
			return Proto::ErrorCode::OK;  // 적 없음 — 조용히 무시

		zone->SpawnHomingProjectile(
			player->GetGuid(), GameObjectType::Player, target->GetGuid(),
			player->GetPosition(), sk->damage, sk->speed, sk->lifetime);
	}
	else  // Skillshot
	{
		float dx = pkt.dir().x();
		float dz = pkt.dir().z();
		const float len = std::sqrt(dx * dx + dz * dz);
		if (len < 1e-4f)
			return Proto::ErrorCode::INVALID_REQUEST;
		dx /= len;
		dz /= len;

		zone->SpawnSkillshotProjectile(
			player->GetGuid(), GameObjectType::Player,
			player->GetPosition(), dx, dz,
			sk->damage, sk->speed, sk->radius, sk->range);
	}

	LOG_INFO("Player " + std::to_string(player->GetPlayerId()) +
		" cast skill: " + sk->name);
	return Proto::ErrorCode::OK;
}
