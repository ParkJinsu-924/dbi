#pragma once

#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "Utils/Logger.h"
#include "Utils/TSingleton.h"
#include "Utils/JobQueue.h"
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
	// When a JobQueue is set, Dispatch enqueues handler execution for deferred
	// processing (GameLoop thread). When null, handlers execute immediately on
	// the calling I/O thread (LoginServer path).
	void SetJobQueue(JobQueue* queue) { jobQueue_ = queue; }

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
	JobQueue* jobQueue_ = nullptr;
};
