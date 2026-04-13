#pragma once

#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Utils/Logger.h"
#include "Utils/TSingleton.h"
#include <google/protobuf/message.h>
#include <concepts>
#include <format>


class PacketSession;

template<typename T>
concept ProtoMessage = std::derived_from<T, google::protobuf::Message>;

template<typename T>
concept PacketSessionType = std::derived_from<T, PacketSession>;

#define GetPacketHandler() PacketHandler::Instance()

class PacketHandler : public TSingleton<PacketHandler>
{
public:
	template<ProtoMessage MsgT, PacketSessionType SessionT>
	void Register(void (*handler)(std::shared_ptr<SessionT>, const MsgT&))
	{
		constexpr uint16 packetId = static_cast<uint16>(PacketIdTraits<MsgT>::Id);
		handlers_[packetId] = [handler](
			std::shared_ptr<PacketSession> session,
			const char* payload, int32 size)
			{
				MsgT msg;
				if (!msg.ParseFromArray(payload, size)) [[unlikely]]
				{
					LOG_ERROR("Failed to parse packet id=" + std::to_string(packetId));
					return;
				}
				
				if (msg.valid() == false)
				{
					LOG_ERROR("Failed to validate packet id=" + std::to_string(packetId));
					return;
				}
				
				handler(std::static_pointer_cast<SessionT>(session), msg);
			};
	}

	void Dispatch(std::shared_ptr<PacketSession> session, uint16 packetId, const char* payload, int32 size);

private:
	using RawHandler = std::function<void(
		std::shared_ptr<PacketSession>, const char*, int32)>;

	std::unordered_map<uint16, RawHandler> handlers_;
};
