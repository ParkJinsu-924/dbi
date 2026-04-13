
#include "GamePacketHandler.h"
#include "Packet/PacketHandler.h"

#define RegisterPacket_Game(RequestPacket, HandlerFunc) \
	GetPacketHandler().Register<RequestPacket, GameSession>(&HandlerFunc);
#define RegisterPacket_Server(Packet, HandlerFunc) \
	GetPacketHandler().Register<Packet, ServerSession>(&HandlerFunc);

struct AutoPacketRegister
{
	AutoPacketRegister()
	{
		// game
		RegisterPacket_Game(Proto::C_EnterGame, GamePacketHandler::C_EnterGame)
		RegisterPacket_Game(Proto::C_PlayerMove, GamePacketHandler::C_PlayerMove)
		RegisterPacket_Game(Proto::C_Chat, GamePacketHandler::C_Chat)
		
		// server
		RegisterPacket_Server(Proto::SS_ValidateTokenResult, GamePacketHandler::SS_ValidateTokenResult)
	}
} _;