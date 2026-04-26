using System;
using System.Buffers;
using System.IO;
using Google.Protobuf;

namespace MMO.Network
{
    // Encodes an outgoing protobuf message into [size][id][payload] in a single contiguous
    // byte buffer rented from ArrayPool<byte>. The TcpSession returns the buffer to the pool
    // after the async send completes, so steady-state allocation is zero.
    public static class PacketSerializer
    {
        public readonly struct EncodedPacket
        {
            public readonly byte[] Buffer;       // pooled — must be returned via ArrayPool<byte>.Shared.Return
            public readonly int Length;          // valid bytes (Buffer may be larger due to pool rounding)

            public EncodedPacket(byte[] buffer, int length)
            {
                Buffer = buffer;
                Length = length;
            }

            public ArraySegment<byte> AsSegment() => new ArraySegment<byte>(Buffer, 0, Length);
        }

        public static EncodedPacket Encode<T>(T message) where T : IMessage<T>
        {
            if (message == null) throw new ArgumentNullException(nameof(message));

            uint packetId = PacketIdMap.GetId<T>();
            int payloadSize = message.CalculateSize();
            int totalSize = PacketHeader.Size + payloadSize;

            if (totalSize > PacketHeader.MaxPacketSize)
                throw new InvalidOperationException(
                    $"outgoing packet too large: total={totalSize} > {PacketHeader.MaxPacketSize}. " +
                    $"type={typeof(T).Name}");

            byte[] buf = ArrayPool<byte>.Shared.Rent(totalSize);
            try
            {
                PacketHeader.Write(buf.AsSpan(0, PacketHeader.Size), (ushort)totalSize, packetId);

                // Write payload directly into the rented buffer at offset 6.
                using (var ms = new MemoryStream(buf, PacketHeader.Size, payloadSize, writable: true))
                using (var cos = new CodedOutputStream(ms))
                {
                    message.WriteTo(cos);
                    cos.Flush();
                }
            }
            catch
            {
                ArrayPool<byte>.Shared.Return(buf);
                throw;
            }
            return new EncodedPacket(buf, totalSize);
        }
    }
}
