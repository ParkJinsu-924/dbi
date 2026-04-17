#include "LoginPacketHandler.h"
#include "Packet/PacketHandler.h"

struct AutoPacketRegister
{
	AutoPacketRegister()
	{
		GetPacketHandler().Register(&LoginPacketHandler::C_Login);
		GetPacketHandler().Register(&LoginPacketHandler::SS_ValidateToken);
	}
} _;