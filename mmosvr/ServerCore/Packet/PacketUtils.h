#pragma once

#include "Utils/Types.h"

// Auto-generated from ShareDir/generate_packet_ids.js
enum class PacketId : uint32
{
	C_LOGIN = 1,
	S_LOGIN = 2,
	S_LOGIN_FAIL = 3,
	C_ENTER_GAME = 4,
	C_PLAYER_MOVE = 5,
	C_CHAT = 6,
	C_REQUEST_USE_SKILL = 7,
	S_ENTER_GAME = 8,
	S_PLAYER_LIST = 9,
	S_PLAYER_MOVE = 10,
	S_CHAT = 11,
	S_PLAYER_LEAVE = 12,
	S_REQUEST_USE_SKILL = 13,
	SS_VALIDATE_TOKEN = 14,
	SS_VALIDATE_TOKEN_RESULT = 15,
};
