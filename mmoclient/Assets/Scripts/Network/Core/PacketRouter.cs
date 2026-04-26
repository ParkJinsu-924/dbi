using System;
using System.Collections.Generic;
using Google.Protobuf;
using UnityEngine;

namespace MMO.Network
{
    // Per-session typed packet dispatch.
    //
    // Register<T>(Action<T> handler) installs:
    //   1. PacketIdMap.Register<T>() so the parser is cached for this id
    //   2. A wrapper that parses the payload on the recv thread and marshals the
    //      decoded message to the main thread before calling the user handler.
    //
    // The recv-thread parse is deliberate: protobuf parsing can be CPU-heavy under
    // load (e.g., big S_PlayerList / S_UnitPositions) and we don't want to occupy
    // Unity's frame budget. Allocations during parse are unavoidable but bounded
    // by message size — Unity's GC is efficient on this scale.
    //
    // Multiple Register<T> calls overwrite the previous handler — there is exactly
    // one handler per packet id, matching the server-side dispatcher convention.
    public sealed class PacketRouter
    {
        // payload is a transient view into TcpSession's recv buffer. The parser MUST
        // copy the bytes it needs into the typed message during ParseFrom — which it
        // does by design — before this delegate returns.
        private readonly Dictionary<uint, Action<ArraySegment<byte>>> _handlers = new();
        private Action<uint> _onUnknownPacket;

        public int HandlerCount => _handlers.Count;

        public void Register<T>(Action<T> handler) where T : IMessage<T>, new()
        {
            if (handler == null) throw new ArgumentNullException(nameof(handler));

            PacketIdMap.Register<T>();
            uint id = PacketIdMap.GetId<T>();

            _handlers[id] = (payload) =>
            {
                // payload is a transient view into TcpSession's recv buffer — we MUST
                // finish parsing here on the recv thread (parser.ParseFrom copies bytes
                // into the message object) BEFORE the next read invalidates the segment.
                T message;
                try
                {
                    if (!PacketIdMap.TryGetParser(id, out var parser))
                    {
                        // Unreachable in practice — Register<T>() above caches the parser.
                        Debug.LogError($"[PacketRouter] parser missing for id={id}");
                        return;
                    }
                    // byte[]+offset+length overload avoids dependence on System.Memory
                    // surface (Span/ReadOnlySpan) which can vary across Unity scripting
                    // backends. Parse copies into the message object — payload is safe
                    // to invalidate after this returns.
                    message = (T)parser.ParseFrom(payload.Array, payload.Offset, payload.Count);
                }
                catch (Exception ex)
                {
                    Debug.LogError($"[PacketRouter] parse failed id={id} type={typeof(T).Name}: {ex.Message}");
                    return;
                }

                MainThreadDispatcher.TryEnqueue(() =>
                {
                    try { handler(message); }
                    catch (Exception ex) { Debug.LogException(ex); }
                });
            };
        }

        // Drop a handler (e.g., on logout). Returns true if a handler was removed.
        public bool Unregister<T>() where T : IMessage<T>
            => _handlers.Remove(PacketIdMap.GetId<T>());

        // Hook for one-off telemetry / unit tests. Receives the unknown id on whichever
        // thread the route was invoked from.
        public void OnUnknownPacket(Action<uint> handler) => _onUnknownPacket = handler;

        // Called by TcpSession.OnPacketReceived. Runs on the recv thread.
        public void Route(uint packetId, ArraySegment<byte> payload)
        {
            if (_handlers.TryGetValue(packetId, out var handler))
            {
                handler(payload);
                return;
            }
            _onUnknownPacket?.Invoke(packetId);
        }

        public void Clear() => _handlers.Clear();
    }
}
