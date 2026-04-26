using System;
using System.Buffers.Binary;

namespace MMO.Network
{
    // Packet wire format: [uint16 size][uint32 id][protobuf payload]
    // size = total packet size including this 6-byte header. All little-endian.
    // Matches mmosvr ServerCore/Packet/PacketHeader.h.
    public static class PacketHeader
    {
        public const int Size = 6;
        public const int SizeFieldOffset = 0;
        public const int SizeFieldLength = 2;
        public const int IdFieldOffset = 2;
        public const int IdFieldLength = 4;

        public const ushort MaxPacketSize = ushort.MaxValue;
        public const int MaxPayloadSize = MaxPacketSize - Size;

        public static void Write(Span<byte> dst, ushort packetSize, uint packetId)
        {
            BinaryPrimitives.WriteUInt16LittleEndian(dst.Slice(SizeFieldOffset, SizeFieldLength), packetSize);
            BinaryPrimitives.WriteUInt32LittleEndian(dst.Slice(IdFieldOffset, IdFieldLength), packetId);
        }

        public static void Read(ReadOnlySpan<byte> src, out ushort packetSize, out uint packetId)
        {
            packetSize = BinaryPrimitives.ReadUInt16LittleEndian(src.Slice(SizeFieldOffset, SizeFieldLength));
            packetId = BinaryPrimitives.ReadUInt32LittleEndian(src.Slice(IdFieldOffset, IdFieldLength));
        }
    }
}
