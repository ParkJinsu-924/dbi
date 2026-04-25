#include "pch.h"
#include "GamePacketHandler.h"
#include "Agent/BuffAgent.h"
#include "Agent/SkillCooldownAgent.h"
#include "Packet/PacketUtils.h"
#include "Packet/PacketHandler.h"
#include "Server/SessionManager.h"
#include "PlayerManager.h"
#include "MapManager.h"
#include "Monster.h"
#include "ZoneManager.h"
#include "ResourceManager.h"
#include "SkillTemplate.h"
#include "SkillRuntime.h"
#include "PacketMaker.h"
#include "GameConstants.h"
#include "Utils/MathUtil.h"
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
	
	// Zone 확보를 Player 생성 전에 선행 — 좀비 Player(zone 미설정 상태 등록) 방지.
	auto* zone = GetZoneManager().GetZone(DEFAULT_ZONE_ID);
	if (!zone)
	{
		SendErrorTo<Proto::C_EnterGame>(gameSession, Proto::ErrorCode::INTERNAL_ERROR);
		LOG_ERROR("DEFAULT_ZONE_ID not available — rejecting C_EnterGame");
		return Proto::ErrorCode::OK;
	}

	const std::string playerName = pkt.username();
	const auto player = GetPlayerManager().CreatePlayerInZone(playerName, *zone, gameSession);
	const int32 playerId = player->GetPlayerId();

	// Notify existing players in the zone about the new arrival.
	// 본인은 아직 S_EnterGame 도 못 받은 시점이라 제외한다
	// (그냥 Broadcast 하면 본인 세션에 S_PlayerSpawn 이 S_EnterGame 보다 먼저 도착해서
	//  클라가 자기 자신을 others 에 등록하는 버그 발생).
	zone->BroadcastExcept(PacketMaker::MakePlayerSpawn(*player), player->GetGuid());

	Proto::Vector2 spawnPos;
	spawnPos.set_x(0.0f);
	spawnPos.set_y(0.0f);
	gameSession->Send(PacketMaker::MakeEnterGame(*player, spawnPos));

	gameSession->Send(PacketMaker::MakePlayerList(GetPlayerManager().GetAllPlayers()));

	gameSession->Send(PacketMaker::MakeMonsterList(zone->GetObjectsByType<Monster>()));

	LOG_INFO("Player entered game: id=" + std::to_string(playerId) + " name=" + playerName);
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_PlayerMove(std::shared_ptr<GameSession> session, const Proto::C_PlayerMove& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	// 이동 CC 차단. 클라에는 별도 응답 없음 — 서버 위치 기준 재동기화는 클라 쪽 책임.
	if (!player->Get<BuffAgent>().CanMove())
		return Proto::ErrorCode::OK;

	Proto::Vector2 validatedPos = pkt.position();

	auto& mapService = GetMapManager();
	if (mapService.IsLoaded() && !mapService.IsOnNavMesh(pkt.position()))
	{
		Proto::Vector2 corrected;
		if (mapService.FindNearestValidPosition(pkt.position(), corrected))
		{
			validatedPos = corrected;
			session->Send(PacketMaker::MakeMoveCorrection(validatedPos));
		}
		else
		{
			return Proto::ErrorCode::INVALID_POSITION;
		}
	}

	player->SetPosition(validatedPos);
	player->SetYaw(pkt.yaw());

	player->GetZone().Broadcast(PacketMaker::MakePlayerMove(*player));
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_MoveCommand(std::shared_ptr<GameSession> /*session*/, const Proto::C_MoveCommand& /*pkt*/)
{
	// 클라-authoritative 이동 모드로 전환됨 (2026-04-23): 서버는 C_PlayerMove 로 들어오는
	// 위치를 권위로 받고 S_PlayerMove 로 브로드캐스트한다. C_MoveCommand 는 수신해도
	// destination 세팅을 안 하므로 서버 tick 이 플레이어를 움직이지 않는다. 호환을 위해 no-op.
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_StopMove(std::shared_ptr<GameSession> session, const Proto::C_StopMove& /*pkt*/)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	// 클라 권위 이동 모델: 서버 destination 개념이 없으므로 별도 상태 정리는 불필요.
	// 정지 시점 최종 위치를 1회 브로드캐스트해서 다른 클라이언트의 보간이 정확히 끝나도록 한다.
	player->GetZone().Broadcast(PacketMaker::MakePlayerMove(*player));

	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_Chat(std::shared_ptr<GameSession> session, const Proto::C_Chat& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	player->GetZone().Broadcast(PacketMaker::MakeChat(*player, pkt.message()));

	LOG_INFO("[Chat] " + player->GetName() + ": " + pkt.message());
	return Proto::ErrorCode::OK;
}

Proto::ErrorCode GamePacketHandler::C_UseSkill(std::shared_ptr<GameSession> session, const Proto::C_UseSkill& pkt)
{
	auto player = GetPlayerManager().FindBySession(session);
	if (!player)
		return Proto::ErrorCode::PLAYER_NOT_FOUND;

	Zone& playerZone = player->GetZone();
	// 스킬 시전 CC 차단 — 조용히 무시 (쿨다운도 소비하지 않음)
	if (!player->Get<BuffAgent>().CanCastSkill())
		return Proto::ErrorCode::OK;

	const auto* skTable = GetResourceManager().Get<SkillTemplate>();
	const SkillTemplate* sk = skTable ? skTable->Find(pkt.skill_id()) : nullptr;
	if (!sk)
	{
		LOG_WARN("C_UseSkill: unknown skill id '" + std::to_string(pkt.skill_id()) + "'");
		return Proto::ErrorCode::INVALID_REQUEST;
	}

	if (!player->Get<SkillCooldownAgent>().TryConsume(sk->sid, sk->cooldown))
		return Proto::ErrorCode::OK;  // 쿨다운 중 — 조용히 무시

	// 클라 권위 이동 모델: 스킬 사용 시 이동 중단은 클라(_cast_skill 의 is_moving=False) 책임.

	switch (sk->targeting)
	{
		case SkillKind::Homing:
		{
			// Homing 은 클라가 대상을 명시 지정. target_guid 생략은 프로토콜 오류.
			if (pkt.target_guid() == 0)
				return Proto::ErrorCode::INVALID_REQUEST;

			auto target = playerZone.FindAs<Monster>(pkt.target_guid());
			if (!target || !target->IsAlive())
				return Proto::ErrorCode::OK;  // 타겟 이미 소멸 — 조용히 무시

			// 서버측 사거리 검증. 클라-서버 위치 예측 race 로 경계부 시전이 miss 되는 것을
			// 방지하기 위해 CAST_RANGE_TOLERANCE 만큼 관용 margin 을 더한다.
			{
				const float effectiveRange = sk->cast_range + GameConfig::CAST_RANGE_TOLERANCE;
				if (player->DistanceToSq(*target) > effectiveRange * effectiveRange)
					return Proto::ErrorCode::OK;
			}

			SkillRuntime::CastTargeted(*sk, *player, *target, playerZone);
		}
		break;
		case SkillKind::Skillshot:
		{
			const auto dir = MathUtil::TryNormalize2D(pkt.dir().x(), pkt.dir().y());
			if (!dir)
				return Proto::ErrorCode::INVALID_REQUEST;

			SkillRuntime::CastSkillshot(*player, dir->x, dir->y, *sk, playerZone);
		}
		break;
		default: ;
	}
	
	return Proto::ErrorCode::OK;
}