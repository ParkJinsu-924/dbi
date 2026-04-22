using System;
using System.Collections.Generic;
using Google.Protobuf;

public class PacketRouter
{
    private readonly Dictionary<uint, Action<byte[]>> handlers = new Dictionary<uint, Action<byte[]>>();

    public void Register<T>(Action<T> handler) where T : IMessage<T>, new()
    {
        var field = typeof(T).GetField("PacketId");
        if (field == null)
        {
            UnityEngine.Debug.LogError("[PacketRouter] No PacketId on " + typeof(T).Name);
            return;
        }
        uint packetId = (uint)field.GetValue(null);

        handlers[packetId] = (payload) =>
        {
            var parser = new MessageParser<T>(() => new T());
            T message = parser.ParseFrom(payload);

            MainThreadDispatcher.Instance.Enqueue(() =>
            {
                handler(message);
            });
        };
    }

    public void Route(uint packetId, byte[] payload)
    {
        if (handlers.TryGetValue(packetId, out var handler))
        {
            handler(payload);
        }
        else
        {
            UnityEngine.Debug.LogWarning("[PacketRouter] Unhandled packet ID: " + packetId);
        }
    }

    public void Clear()
    {
        handlers.Clear();
    }
}
