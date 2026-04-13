#include "GamePacketHandler.h"
#include "Packet/PacketHandler.h"

// All handlers return Proto::ErrorCode. On non-OK return, the dispatcher
// (PacketHandler::Register) automatically sends an S_Error packet back to the
// requesting session with source_packet_id set to the request's PacketId.
//
// For broadcast or receive-only handlers (e.g. C_PlayerMove, SS_*), return OK
// after completing the side effect. Reserve non-OK for actual failures.

#define RegisterPacket_Game(Packet, HandlerFunc) \
	GetPacketHandler().Register<Packet, GameSession>(&HandlerFunc);
#define RegisterPacket_Server(Packet, HandlerFunc) \
	GetPacketHandler().Register<Packet, ServerSession>(&HandlerFunc);

struct AutoPacketRegister
{
	AutoPacketRegister()
	{
		// Game (client → server)
		RegisterPacket_Game(Proto::C_EnterGame, GamePacketHandler::C_EnterGame)
		RegisterPacket_Game(Proto::C_PlayerMove, GamePacketHandler::C_PlayerMove)
		RegisterPacket_Game(Proto::C_Chat, GamePacketHandler::C_Chat)

		// Server (LoginServer → GameServer)
		RegisterPacket_Server(Proto::SS_ValidateTokenResult, GamePacketHandler::SS_ValidateTokenResult)
	}
} _;
