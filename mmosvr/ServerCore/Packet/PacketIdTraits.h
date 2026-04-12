#pragma once

#include "Packet/PacketUtils.h"

// Auto-generated from ShareDir/generate_packet_ids.js

namespace Proto
{
	class C_Login;
	class S_Login;
	class S_LoginFail;
	class C_EnterGame;
	class C_PlayerMove;
	class C_Chat;
	class C_RequestUseSkill;
	class S_EnterGame;
	class S_PlayerList;
	class S_PlayerMove;
	class S_Chat;
	class S_PlayerLeave;
	class S_RequestUseSkill;
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
PACKET_ID_TRAIT(S_LoginFail, S_LOGIN_FAIL)
PACKET_ID_TRAIT(C_EnterGame, C_ENTER_GAME)
PACKET_ID_TRAIT(C_PlayerMove, C_PLAYER_MOVE)
PACKET_ID_TRAIT(C_Chat, C_CHAT)
PACKET_ID_TRAIT(C_RequestUseSkill, C_REQUEST_USE_SKILL)
PACKET_ID_TRAIT(S_EnterGame, S_ENTER_GAME)
PACKET_ID_TRAIT(S_PlayerList, S_PLAYER_LIST)
PACKET_ID_TRAIT(S_PlayerMove, S_PLAYER_MOVE)
PACKET_ID_TRAIT(S_Chat, S_CHAT)
PACKET_ID_TRAIT(S_PlayerLeave, S_PLAYER_LEAVE)
PACKET_ID_TRAIT(S_RequestUseSkill, S_REQUEST_USE_SKILL)
PACKET_ID_TRAIT(SS_ValidateToken, SS_VALIDATE_TOKEN)
PACKET_ID_TRAIT(SS_ValidateTokenResult, SS_VALIDATE_TOKEN_RESULT)

#undef PACKET_ID_TRAIT
