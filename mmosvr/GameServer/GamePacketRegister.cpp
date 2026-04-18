#include "GamePacketHandler.h"
#include "Packet/PacketHandler.h"

// All handlers return Proto::ErrorCode. On non-OK return, the dispatcher
// (PacketHandler::Register) automatically sends an S_Error packet back to the
// requesting session with source_packet_id set to the request's PacketId.
//
// For broadcast or receive-only handlers (e.g. C_PlayerMove, SS_*), return OK
// after completing the side effect. Reserve non-OK for actual failures.

struct AutoPacketRegister
{
	AutoPacketRegister()
	{
		// Game (client → server)
		GetPacketHandler().Register(&GamePacketHandler::C_EnterGame);
		GetPacketHandler().Register(&GamePacketHandler::C_PlayerMove);
		GetPacketHandler().Register(&GamePacketHandler::C_MoveCommand);
		GetPacketHandler().Register(&GamePacketHandler::C_StopMove);
		GetPacketHandler().Register(&GamePacketHandler::C_Chat);
		GetPacketHandler().Register(&GamePacketHandler::C_RequestUseSkill);

		// Server (LoginServer → GameServer)
		GetPacketHandler().Register(&GamePacketHandler::SS_ValidateToken);
	}
} _;
