#include "pch.h"
#include "Network/PacketSession.h"


int32 PacketSession::OnRecv(char* buffer, int32 len)
{
	int32 consumed = 0;

	while (len - consumed >= PACKET_HEADER_SIZE)
	{
		auto* header = reinterpret_cast<const PacketHeader*>(buffer + consumed);

		if (header->size < PACKET_HEADER_SIZE || header->size > 65535) [[unlikely]]
		{
			return -1;  // malformed packet
		}

		if (len - consumed < static_cast<int32>(header->size))
		{
			break;  // incomplete packet, wait for more data
		}

		const char* payload = buffer + consumed + PACKET_HEADER_SIZE;
		int32 payloadSize = static_cast<int32>(header->size) - PACKET_HEADER_SIZE;

		OnRecvPacket(header->id, payload, payloadSize);

		consumed += header->size;
	}

	return consumed;
}

SendBufferChunkPtr PacketSession::MakeSendBufferRaw(
	uint16 packetId, const google::protobuf::Message& msg)
{
	int32 payloadSize = static_cast<int32>(msg.ByteSizeLong());
	int32 totalSize = PACKET_HEADER_SIZE + payloadSize;

	auto chunk = std::make_shared<SendBufferChunk>(totalSize);

	PacketHeader header;
	header.size = static_cast<uint16>(totalSize);
	header.id = packetId;
	std::memcpy(chunk->Buffer(), &header, PACKET_HEADER_SIZE);

	msg.SerializeToArray(chunk->Buffer() + PACKET_HEADER_SIZE, payloadSize);

	chunk->SetSize(totalSize);
	return chunk;
}
