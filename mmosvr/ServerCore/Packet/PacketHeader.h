#pragma once

#include "Utils/Types.h"


#pragma pack(push, 1)
struct PacketHeader
{
	uint16 size;    // total packet size (header + payload)
	uint32 id;      // packet type ID
};
#pragma pack(pop)

constexpr int32 PACKET_HEADER_SIZE = sizeof(PacketHeader);  // 6 bytes
