using System;
using System.Collections.Concurrent;
using System.Reflection;
using Google.Protobuf;

namespace MMO.Network
{
    // Resolves protobuf message Type ↔ uint PacketId via reflection on the auto-generated
    // `public const uint PacketId` constant in Proto/PacketIds.cs. Reflection happens once
    // per type; subsequent lookups are dictionary-only (constant-time, no GC).
    //
    // Receive parsers must be registered explicitly via Register<T>() so that an unknown
    // packet id from the server can be detected and logged rather than silently dropped.
    public static class PacketIdMap
    {
        private static readonly ConcurrentDictionary<Type, uint> _idByType = new();
        private static readonly ConcurrentDictionary<uint, MessageParser> _parserById = new();

        public static uint GetId<T>() where T : IMessage<T>
            => _idByType.GetOrAdd(typeof(T), ResolveIdFromConstant);

        public static uint GetId(Type type)
            => _idByType.GetOrAdd(type, ResolveIdFromConstant);

        public static bool TryGetParser(uint id, out MessageParser parser)
            => _parserById.TryGetValue(id, out parser);

        // Cache the parser singleton (T.Parser) for fast deserialize-by-id on the recv thread.
        // Generated protobuf classes expose `public static MessageParser<T> Parser`.
        public static void Register<T>() where T : IMessage<T>, new()
        {
            uint id = GetId<T>();
            var parserField = typeof(T).GetProperty("Parser", BindingFlags.Public | BindingFlags.Static);
            if (parserField == null)
                throw new InvalidOperationException(
                    $"Protobuf message {typeof(T).Name} has no static Parser property");
            var parser = (MessageParser)parserField.GetValue(null);
            _parserById[id] = parser;
        }

        public static bool IsRegistered(uint id) => _parserById.ContainsKey(id);

        private static uint ResolveIdFromConstant(Type type)
        {
            // PacketIds.cs declares: public partial class XXX { public const uint PacketId = N; }
            var field = type.GetField("PacketId", BindingFlags.Public | BindingFlags.Static);
            if (field == null || field.FieldType != typeof(uint))
                throw new InvalidOperationException(
                    $"Protobuf message {type.Name} has no `public const uint PacketId` " +
                    $"(generated from ShareDir/generate_packet_ids.js)");
            return (uint)field.GetValue(null);
        }
    }
}
