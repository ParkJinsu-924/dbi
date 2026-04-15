#pragma once

#include "Network/Session.h"
#include "Packet/PacketHeader.h"
#include "Packet/PacketIdTraits.h"
#include "common.pb.h"


class PacketSession : public Session
{
public:
	using Session::Session;

	template<typename T>
	void Send(const T& msg)
	{
		constexpr uint16 packetId = static_cast<uint16>(PacketIdTraits<T>::Id);
		Session::Send(MakeSendBufferRaw(packetId, msg));
	}

	template<typename T>
	SendBufferChunkPtr MakeSendBuffer(const T& msg)
	{
		constexpr uint16 packetId = static_cast<uint16>(PacketIdTraits<T>::Id);
		return MakeSendBufferRaw(packetId, msg);
	}

protected:
	virtual void OnRecvPacket(uint16 packetId, const char* payload, int32 payloadSize) = 0;

private:
	int32 OnRecv(char* buffer, int32 len) override;

	static SendBufferChunkPtr MakeSendBufferRaw(uint16 packetId, const google::protobuf::Message& msg);
};
