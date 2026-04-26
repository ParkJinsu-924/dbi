using System;

namespace MMO.Network
{
    // Append-and-compact byte buffer for TCP framing. The TCP byte stream may deliver a
    // packet split across multiple recv() calls, or multiple packets coalesced into one —
    // this class accumulates bytes and yields complete framed packets via TryReadPacket.
    //
    // Memory shape: one fixed-capacity backing array. We append at _writePos; once a
    // packet is consumed we either compact (memmove leftover to head) or, if the buffer
    // is empty, just reset offsets to zero. The capacity must be >= max packet size.
    //
    // Single-producer/single-consumer assumed (the recv thread). Not thread-safe.
    public sealed class ReceiveBuffer
    {
        private readonly byte[] _buf;
        private int _readPos;
        private int _writePos;

        public ReceiveBuffer(int capacity)
        {
            if (capacity < PacketHeader.Size)
                throw new ArgumentOutOfRangeException(nameof(capacity),
                    $"capacity must be >= {PacketHeader.Size} (header size)");
            _buf = new byte[capacity];
        }

        public int Capacity => _buf.Length;
        public int BytesUsed => _writePos - _readPos;
        public int FreeSpace => _buf.Length - _writePos;

        // Returns the writable region for socket Receive(). Caller MUST call Commit(n)
        // with the actual byte count after a successful read, or no bytes are consumed.
        public ArraySegment<byte> GetWriteSegment()
        {
            CompactIfNeeded();
            return new ArraySegment<byte>(_buf, _writePos, _buf.Length - _writePos);
        }

        public void Commit(int byteCount)
        {
            if (byteCount < 0 || byteCount > _buf.Length - _writePos)
                throw new ArgumentOutOfRangeException(nameof(byteCount));
            _writePos += byteCount;
        }

        // Pull one complete packet off the head of the buffer. Returns false if not enough
        // bytes have arrived. Throws on protocol violation (malformed size).
        //
        // payload is a view into the internal buffer — valid only until the next call to
        // GetWriteSegment / TryReadPacket. Callers that need to retain the payload must
        // copy it (e.g., parse with the protobuf MessageParser, which copies into the
        // generated message object).
        public bool TryReadPacket(int maxPayloadSize, out uint packetId, out ArraySegment<byte> payload)
        {
            packetId = 0;
            payload = default;

            int used = BytesUsed;
            if (used < PacketHeader.Size) return false;

            ReadOnlySpan<byte> headerSpan = new ReadOnlySpan<byte>(_buf, _readPos, PacketHeader.Size);
            PacketHeader.Read(headerSpan, out ushort packetSize, out uint id);

            if (packetSize < PacketHeader.Size)
                throw new InvalidOperationException(
                    $"corrupt packet: size={packetSize} < header({PacketHeader.Size})");

            int payloadSize = packetSize - PacketHeader.Size;
            if (payloadSize > maxPayloadSize)
                throw new InvalidOperationException(
                    $"packet too large: payload={payloadSize} > max({maxPayloadSize}). " +
                    $"id={id}. Likely framing corruption.");

            if (used < packetSize) return false;   // wait for more bytes

            packetId = id;
            payload = new ArraySegment<byte>(_buf, _readPos + PacketHeader.Size, payloadSize);
            _readPos += packetSize;
            return true;
        }

        // Slide unread bytes to the head when free space is exhausted.
        // Cheap because we usually compact only after consuming several packets.
        private void CompactIfNeeded()
        {
            if (_readPos == 0) return;

            int used = _writePos - _readPos;
            if (used == 0)
            {
                _readPos = 0;
                _writePos = 0;
                return;
            }

            // Only memmove when we actually need free space at the tail.
            if (FreeSpace > 0) return;

            Buffer.BlockCopy(_buf, _readPos, _buf, 0, used);
            _readPos = 0;
            _writePos = used;
        }
    }
}
