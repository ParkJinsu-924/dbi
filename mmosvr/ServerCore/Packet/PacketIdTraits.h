#pragma once

#include "Packet/PacketUtils.h"

// Auto-generated from ShareDir/generate_packet_ids.js

namespace Proto
{
	class C_Login;
	class S_Login;
	class C_EnterGame;
	class C_PlayerMove;
	class C_Chat;
	class C_RequestUseSkill;
	class C_MoveCommand;
	class C_StopMove;
	class S_EnterGame;
	class S_PlayerList;
	class S_PlayerMove;
	class S_Chat;
	class S_PlayerLeave;
	class S_PlayerSpawn;
	class S_RequestUseSkill;
	class S_MoveCorrection;
	class S_Error;
	class S_MonsterSpawn;
	class S_MonsterMove;
	class S_MonsterDespawn;
	class S_MonsterList;
	class S_MonsterState;
	class S_MonsterAttack;
	class S_HitscanAttack;
	class S_UnitHp;
	class S_ProjectileSpawn;
	class S_ProjectileHit;
	class S_ProjectileDestroy;
	class SS_ValidateToken;
	class SS_ValidateTokenResult;
}

template<typename T>
struct PacketIdTraits;

#define PACKET_ID_TRAIT(MsgType, PktId) \
template<> struct PacketIdTraits<Proto::MsgType> \
{ static constexpr PacketId Id = PacketId::PktId; };

PACKET_ID_TRAIT(C_Login, C_LOGIN)
PACKET_ID_TRAIT(S_Login, S_LOGIN)
PACKET_ID_TRAIT(C_EnterGame, C_ENTER_GAME)
PACKET_ID_TRAIT(C_PlayerMove, C_PLAYER_MOVE)
PACKET_ID_TRAIT(C_Chat, C_CHAT)
PACKET_ID_TRAIT(C_RequestUseSkill, C_REQUEST_USE_SKILL)
PACKET_ID_TRAIT(C_MoveCommand, C_MOVE_COMMAND)
PACKET_ID_TRAIT(C_StopMove, C_STOP_MOVE)
PACKET_ID_TRAIT(S_EnterGame, S_ENTER_GAME)
PACKET_ID_TRAIT(S_PlayerList, S_PLAYER_LIST)
PACKET_ID_TRAIT(S_PlayerMove, S_PLAYER_MOVE)
PACKET_ID_TRAIT(S_Chat, S_CHAT)
PACKET_ID_TRAIT(S_PlayerLeave, S_PLAYER_LEAVE)
PACKET_ID_TRAIT(S_PlayerSpawn, S_PLAYER_SPAWN)
PACKET_ID_TRAIT(S_RequestUseSkill, S_REQUEST_USE_SKILL)
PACKET_ID_TRAIT(S_MoveCorrection, S_MOVE_CORRECTION)
PACKET_ID_TRAIT(S_Error, S_ERROR)
PACKET_ID_TRAIT(S_MonsterSpawn, S_MONSTER_SPAWN)
PACKET_ID_TRAIT(S_MonsterMove, S_MONSTER_MOVE)
PACKET_ID_TRAIT(S_MonsterDespawn, S_MONSTER_DESPAWN)
PACKET_ID_TRAIT(S_MonsterList, S_MONSTER_LIST)
PACKET_ID_TRAIT(S_MonsterState, S_MONSTER_STATE)
PACKET_ID_TRAIT(S_MonsterAttack, S_MONSTER_ATTACK)
PACKET_ID_TRAIT(S_HitscanAttack, S_HITSCAN_ATTACK)
PACKET_ID_TRAIT(S_UnitHp, S_UNIT_HP)
PACKET_ID_TRAIT(S_ProjectileSpawn, S_PROJECTILE_SPAWN)
PACKET_ID_TRAIT(S_ProjectileHit, S_PROJECTILE_HIT)
PACKET_ID_TRAIT(S_ProjectileDestroy, S_PROJECTILE_DESTROY)
PACKET_ID_TRAIT(SS_ValidateToken, SS_VALIDATE_TOKEN)
PACKET_ID_TRAIT(SS_ValidateTokenResult, SS_VALIDATE_TOKEN_RESULT)

#undef PACKET_ID_TRAIT
