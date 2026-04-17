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

	if (jobQueue_)
	{
		// Copy payload — the original buffer (RecvBuffer) will be reused
		// after this function returns, before GameLoop processes the job.
		auto payloadCopy = std::make_shared<std::vector<char>>(payload, payload + size);
		auto handler = it->second;

		jobQueue_->Push([session, handler, payloadCopy, size]()
			{
				handler(session, payloadCopy->data(), size);
			});
	}
	else
	{
		// No job queue — execute immediately on I/O thread (LoginServer path)
		it->second(session, payload, size);
	}
}
