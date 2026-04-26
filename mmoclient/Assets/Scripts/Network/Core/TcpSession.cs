using System;
using System.Buffers;
using System.Net.Sockets;
using System.Threading;
using System.Collections.Concurrent;

namespace MMO.Network
{
    // Production-grade TCP session for an MMO client.
    //   - Single dedicated recv thread feeding a ReceiveBuffer (TCP framing).
    //   - Single dedicated send thread draining a BlockingCollection — callers Send() never block
    //     the game thread on socket writes; back-pressure surfaces as TryAdd failure.
    //   - Both threads share a CancellationTokenSource so Close() unblocks them quickly.
    //   - Buffers for outgoing packets are rented from ArrayPool and returned after flush.
    //
    // Events are raised on whichever background thread detected the condition. Consumers that
    // touch Unity API (GameObjects, etc.) must marshal to the main thread via PacketRouter
    // (which uses MainThreadDispatcher for handler dispatch).
    public sealed class TcpSession : IDisposable
    {
        private readonly int _recvBufferSize;
        private readonly int _maxPayloadSize;
        private readonly NetworkStats _stats = new();

        private TcpClient _client;
        private NetworkStream _stream;
        private CancellationTokenSource _cts;
        private Thread _recvThread;
        private Thread _sendThread;
        private BlockingCollection<PacketSerializer.EncodedPacket> _sendQueue;
        private int _state;                           // ConnectionState as int (Interlocked)
        private int _disposed;                        // 0 = alive, 1 = disposed

        public ConnectionState State => (ConnectionState)Volatile.Read(ref _state);
        public bool IsConnected => State == ConnectionState.Connected;
        public NetworkStats Stats => _stats;
        public string Endpoint { get; private set; }

        // Raised on background threads. Always wrap UI/scene work via MainThreadDispatcher.
        public event Action OnConnected;
        public event Action<DisconnectReason, Exception> OnDisconnected;
        public event Action<uint, ArraySegment<byte>> OnPacketReceived;

        public TcpSession(int recvBufferSize, int maxPayloadSize, int sendQueueCapacity)
        {
            if (recvBufferSize <= PacketHeader.Size)
                throw new ArgumentOutOfRangeException(nameof(recvBufferSize));
            if (maxPayloadSize <= 0 || maxPayloadSize > PacketHeader.MaxPayloadSize)
                throw new ArgumentOutOfRangeException(nameof(maxPayloadSize));
            if (sendQueueCapacity <= 0)
                throw new ArgumentOutOfRangeException(nameof(sendQueueCapacity));

            _recvBufferSize = recvBufferSize;
            _maxPayloadSize = maxPayloadSize;
            _sendQueue = new BlockingCollection<PacketSerializer.EncodedPacket>(
                new ConcurrentQueue<PacketSerializer.EncodedPacket>(),
                boundedCapacity: sendQueueCapacity);
        }

        // Synchronous TCP connect with a timeout. Throws on failure (caller catches and
        // converts to OnDisconnected(ConnectFailed) if desired). Spins up I/O threads on success.
        public void Connect(string host, int port, TimeSpan timeout)
        {
            if (Interlocked.CompareExchange(ref _state, (int)ConnectionState.Connecting,
                                                       (int)ConnectionState.Disconnected)
                != (int)ConnectionState.Disconnected)
                throw new InvalidOperationException("Connect called in non-Disconnected state");

            try
            {
                _client = new TcpClient { NoDelay = true };
                Endpoint = $"{host}:{port}";

                // BeginConnect + AsyncWaitHandle gives us a hard timeout. Synchronous
                // TcpClient.Connect ignores SendTimeout/ReceiveTimeout for the SYN handshake.
                var connect = _client.BeginConnect(host, port, null, null);
                bool ok = connect.AsyncWaitHandle.WaitOne(timeout);
                if (!ok)
                {
                    SafeClose(_client);
                    Volatile.Write(ref _state, (int)ConnectionState.Disconnected);
                    throw new TimeoutException($"connect to {Endpoint} timed out after {timeout.TotalSeconds:0.#}s");
                }
                _client.EndConnect(connect);
                _stream = _client.GetStream();

                _cts = new CancellationTokenSource();

                _recvThread = new Thread(RecvLoop)
                {
                    IsBackground = true,
                    Name = $"Net-Recv[{Endpoint}]"
                };
                _sendThread = new Thread(SendLoop)
                {
                    IsBackground = true,
                    Name = $"Net-Send[{Endpoint}]"
                };

                Volatile.Write(ref _state, (int)ConnectionState.Connected);
                _recvThread.Start();
                _sendThread.Start();
                OnConnected?.Invoke();
            }
            catch
            {
                Volatile.Write(ref _state, (int)ConnectionState.Disconnected);
                SafeClose(_client);
                _client = null;
                _stream = null;
                throw;
            }
        }

        // Encode + enqueue a typed message. Returns false if the queue is full (back-pressure)
        // or the session is not connected. Game code should treat false as a soft drop and
        // ideally not call Send while Disconnected.
        public bool Send<T>(T message) where T : Google.Protobuf.IMessage<T>
        {
            if (State != ConnectionState.Connected) return false;

            var encoded = PacketSerializer.Encode(message);
            // TryAdd is safe even after CompleteAdding — returns false instead of throwing.
            bool added;
            try
            {
                added = _sendQueue.TryAdd(encoded);
            }
            catch (InvalidOperationException)
            {
                added = false;
            }
            if (!added)
            {
                ArrayPool<byte>.Shared.Return(encoded.Buffer);
                return false;
            }
            _stats.OnEnqueued();
            return true;
        }

        public void Close(DisconnectReason reason = DisconnectReason.Local)
        {
            // Fast-path: idempotent. Only the first caller transitions Connected/Connecting → Closing.
            int prev = Interlocked.Exchange(ref _state, (int)ConnectionState.Closing);
            if (prev == (int)ConnectionState.Disconnected || prev == (int)ConnectionState.Closing)
                return;

            try { _cts?.Cancel(); } catch { }
            // CompleteAdding wakes the send thread blocked on Take().
            try { _sendQueue?.CompleteAdding(); } catch { }
            // Closing the stream/client breaks any blocking Read on the recv thread.
            SafeClose(_client);

            // Drain remaining queued buffers to avoid leaking pool memory.
            try
            {
                while (_sendQueue != null && _sendQueue.TryTake(out var pending))
                    ArrayPool<byte>.Shared.Return(pending.Buffer);
            }
            catch { }

            Volatile.Write(ref _state, (int)ConnectionState.Disconnected);
            OnDisconnected?.Invoke(reason, null);
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 1) return;
            Close(DisconnectReason.Local);
            try { _sendQueue?.Dispose(); } catch { }
            try { _cts?.Dispose(); } catch { }
            _sendQueue = null;
            _cts = null;
        }

        // ────────────────────────────────────────────────────────────────────────
        //  Recv loop — owns one ReceiveBuffer, parses framed packets, raises events.
        // ────────────────────────────────────────────────────────────────────────
        private void RecvLoop()
        {
            var buffer = new ReceiveBuffer(_recvBufferSize);
            DisconnectReason reason = DisconnectReason.RemoteClosed;
            Exception error = null;
            try
            {
                while (!_cts.IsCancellationRequested)
                {
                    var seg = buffer.GetWriteSegment();
                    if (seg.Count == 0)
                    {
                        // Buffer is full of unconsumed bytes — capacity is smaller than the
                        // current packet. Either the recv buffer was misconfigured or the
                        // stream is corrupt. Either way, bail.
                        reason = DisconnectReason.ProtocolError;
                        error = new InvalidOperationException(
                            $"recv buffer full; capacity={buffer.Capacity}, used={buffer.BytesUsed}. " +
                            "Increase RecvBufferSize or check for stream corruption.");
                        break;
                    }

                    int n;
                    try
                    {
                        n = _stream.Read(seg.Array, seg.Offset, seg.Count);
                    }
                    catch (Exception ex) when (_cts.IsCancellationRequested
                                            || State == ConnectionState.Closing)
                    {
                        // Local close — silent shutdown.
                        return;
                    }
                    catch (Exception ex)
                    {
                        reason = DisconnectReason.RecvError;
                        error = ex;
                        break;
                    }

                    if (n <= 0)
                    {
                        reason = DisconnectReason.RemoteClosed;
                        break;
                    }

                    buffer.Commit(n);

                    // Drain all complete packets currently in the buffer.
                    while (true)
                    {
                        bool more;
                        try
                        {
                            more = buffer.TryReadPacket(_maxPayloadSize, out uint id, out var payload);
                            if (!more) break;
                            _stats.OnReceived(payload.Count + PacketHeader.Size);
                            OnPacketReceived?.Invoke(id, payload);
                        }
                        catch (Exception ex)
                        {
                            // Framing corruption — the stream is no longer trustworthy.
                            reason = DisconnectReason.ProtocolError;
                            error = ex;
                            goto done;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                reason = DisconnectReason.RecvError;
                error = ex;
            }
        done:
            HandleBackgroundShutdown(reason, error);
        }

        // ────────────────────────────────────────────────────────────────────────
        //  Send loop — drains the queue, writes to the stream, returns buffers to the pool.
        // ────────────────────────────────────────────────────────────────────────
        private void SendLoop()
        {
            DisconnectReason reason = DisconnectReason.Local;
            Exception error = null;
            try
            {
                foreach (var pkt in _sendQueue.GetConsumingEnumerable(_cts.Token))
                {
                    try
                    {
                        _stream.Write(pkt.Buffer, 0, pkt.Length);
                        _stats.OnSent(pkt.Length);
                    }
                    catch (Exception ex)
                    {
                        if (!_cts.IsCancellationRequested && State != ConnectionState.Closing)
                        {
                            reason = DisconnectReason.SendError;
                            error = ex;
                        }
                        ArrayPool<byte>.Shared.Return(pkt.Buffer);
                        _stats.OnDequeued();
                        break;
                    }
                    ArrayPool<byte>.Shared.Return(pkt.Buffer);
                    _stats.OnDequeued();
                }
            }
            catch (OperationCanceledException) { /* clean shutdown */ }
            catch (Exception ex)
            {
                reason = DisconnectReason.SendError;
                error = ex;
            }
            HandleBackgroundShutdown(reason, error);
        }

        // Either of the two threads detecting an error/EOF triggers a single shutdown sequence.
        // Interlocked guards against double OnDisconnected raise.
        private void HandleBackgroundShutdown(DisconnectReason reason, Exception error)
        {
            int prev = Interlocked.CompareExchange(ref _state,
                (int)ConnectionState.Closing, (int)ConnectionState.Connected);
            if (prev != (int)ConnectionState.Connected)
                return;   // Close() or the other thread already handled it

            try { _cts?.Cancel(); } catch { }
            try { _sendQueue?.CompleteAdding(); } catch { }
            SafeClose(_client);

            try
            {
                while (_sendQueue != null && _sendQueue.TryTake(out var pending))
                    ArrayPool<byte>.Shared.Return(pending.Buffer);
            }
            catch { }

            Volatile.Write(ref _state, (int)ConnectionState.Disconnected);
            OnDisconnected?.Invoke(reason, error);
        }

        private static void SafeClose(TcpClient c)
        {
            try { c?.Close(); } catch { }
        }
    }
}
