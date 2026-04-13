#pragma once

#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Utils/Logger.h"
#include "Utils/TSingleton.h"
#include "common.pb.h"
#include "game.pb.h"
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
	// Register a handler that returns ErrorCode.
	// On non-OK return, an S_Error packet is automatically sent back to the requesting session
	// with source_packet_id set to the request's PacketId.
	//
	// Convention:
	//   - Handlers should EARLY RETURN on error — do NOT send any response packet before
	//     returning a non-OK code (otherwise the client receives both partial response and S_Error).
	//   - For broadcast/receive-only handlers (e.g. C_PlayerMove, SS_*), return OK after
	//     completing the side effect; non-OK is reserved for actual failures.
	//   - For ServerSession handlers, returning non-OK sends S_Error back to the peer server,
	//     which is intentional for surfacing protocol bugs in server-to-server links.
	template<ProtoMessage MsgT, PacketSessionType SessionT>
	void Register(Proto::ErrorCode (*handler)(std::shared_ptr<SessionT>, const MsgT&))
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

				auto typedSession = std::static_pointer_cast<SessionT>(session);
				Proto::ErrorCode err = handler(typedSession, msg);

				if (err != Proto::ErrorCode::OK) [[unlikely]]
				{
					Proto::S_Error errPkt;
					errPkt.set_source_packet_id(packetId);
					errPkt.set_code(err);
					typedSession->Send(errPkt);
				}
			};
	}

	void Dispatch(std::shared_ptr<PacketSession> session, uint16 packetId, const char* payload, int32 size);

private:
	using RawHandler = std::function<void(
		std::shared_ptr<PacketSession>, const char*, int32)>;

	std::unordered_map<uint16, RawHandler> handlers_;
};
