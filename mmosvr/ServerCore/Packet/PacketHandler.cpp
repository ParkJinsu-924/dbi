#include "pch.h"
#include "Packet/PacketHandler.h"


void PacketHandler::Dispatch(std::shared_ptr<PacketSession> session,
	uint16 packetId, const char* payload, int32 size)
{
	auto it = handlers_.find(packetId);
	if (it == handlers_.end()) [[unlikely]]
	{
		LOG_WARN("Unhandled packet id=" + std::to_string(packetId));
		return;
	}
	it->second(session, payload, size);
}
