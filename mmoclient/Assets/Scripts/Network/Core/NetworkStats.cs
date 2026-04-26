using System.Threading;

namespace MMO.Network
{
    // Thread-safe counters updated by send/recv loops; read by main thread for HUD/diagnostics.
    // Atomic ops (Interlocked) keep the cost under a nanosecond per update — far cheaper than locks.
    public sealed class NetworkStats
    {
        private long _bytesSent;
        private long _bytesReceived;
        private long _packetsSent;
        private long _packetsReceived;
        private long _sendQueueDepth;     // number of packets queued but not yet flushed to socket

        public long BytesSent => Interlocked.Read(ref _bytesSent);
        public long BytesReceived => Interlocked.Read(ref _bytesReceived);
        public long PacketsSent => Interlocked.Read(ref _packetsSent);
        public long PacketsReceived => Interlocked.Read(ref _packetsReceived);
        public long SendQueueDepth => Interlocked.Read(ref _sendQueueDepth);

        internal void OnSent(int byteCount)
        {
            Interlocked.Add(ref _bytesSent, byteCount);
            Interlocked.Increment(ref _packetsSent);
        }

        internal void OnReceived(int byteCount)
        {
            Interlocked.Add(ref _bytesReceived, byteCount);
            Interlocked.Increment(ref _packetsReceived);
        }

        internal void OnEnqueued() => Interlocked.Increment(ref _sendQueueDepth);
        internal void OnDequeued() => Interlocked.Decrement(ref _sendQueueDepth);

        public void Reset()
        {
            Interlocked.Exchange(ref _bytesSent, 0);
            Interlocked.Exchange(ref _bytesReceived, 0);
            Interlocked.Exchange(ref _packetsSent, 0);
            Interlocked.Exchange(ref _packetsReceived, 0);
            Interlocked.Exchange(ref _sendQueueDepth, 0);
        }
    }
}
