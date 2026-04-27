using System;
using Proto;
using UnityEngine;

namespace MMO.Network
{
    // Owns the GameServer TCP connection and routes typed packets to gameplay-layer events.
    //
    // After connect, the client sends C_EnterGame(token); the server validates the token
    // (via its server-to-server link to LoginServer) and replies with S_EnterGame.
    // Until S_EnterGame arrives we are connected at the TCP level but not yet "in game" —
    // gameplay packets like C_PlayerMove must be gated on IsInGame, which the base
    // Session.TrySend honors via the IsReady override below.
    //
    // Server-push gameplay packets are exposed as multicast events on a single,
    // app-lifetime PacketHandlers singleton (Net.Handlers) — subscribers reach them via:
    //     Net.Handlers.OnPlayerSpawn += ...
    //     Net.Handlers.OnMonsterMove += ...
    // GameSession does NOT own its own PacketHandlers — it binds Net.Handlers's events
    // to its private PacketRouter via HandlerAutoRegistrar. This way a logout/reconnect
    // doesn't invalidate existing subscribers.
    //
    // Client→server typed senders live in PacketSenders.cs (accessed via
    // Net.Senders.SendXxx(...)). Adding a new packet means one line in either
    // Session/PacketHandlers.cs (server-push) or Session/PacketSenders.cs (client-send).
    //
    // Common machinery (TcpSession ownership, PacketRouter, Connect/Close/Dispose,
    // TrySend, OnConnected/OnDisconnected) lives on the Session base class.
    public sealed class GameSession : Session
    {
        public bool IsInGame { get; private set; }
        public int LocalPlayerId { get; private set; }
        public long LocalPlayerGuid { get; private set; }
        public Vector3 LocalSpawnPosition { get; private set; }

        // Game-specific events (main thread).
        public event Action<S_EnterGame> OnEnteredGame;
        public event Action<S_Error> OnServerError;

        protected override string LogTag => "Game";

        // Gameplay packets only after the EnterGame handshake completes.
        protected override bool IsReady => IsInGame;

        public GameSession(NetworkConfig config) : base(config) { }

        protected override void RegisterHandlers()
        {
            // Session lifecycle — kept inline because these mutate session-owned
            // state (IsInGame, LocalPlayerId, ...) before fanning out to subscribers.
            _router.Register<S_EnterGame>(msg =>
            {
                LocalPlayerId = msg.PlayerId;
                Debug.Log($"[>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>Game] entered as playerId={msg.PlayerId}");
                LocalPlayerGuid = msg.Guid;
                LocalSpawnPosition = new Vector3(
                    msg.SpawnPosition?.X ?? 0f, 0f, msg.SpawnPosition?.Y ?? 0f);
                IsInGame = true;
                if (_config.LogLifecycle)
                    Debug.Log($"[Game] entered as playerId={LocalPlayerId} guid={LocalPlayerGuid} " +
                              $"spawn=({LocalSpawnPosition.x:F1},{LocalSpawnPosition.z:F1})");
                OnEnteredGame?.Invoke(msg);
            });

            _router.Register<S_Error>(msg =>
            {
                Debug.LogError($"[Game] server error src={msg.SourcePacketId} code={msg.Code} detail={msg.Detail}");
                OnServerError?.Invoke(msg);
            });

            // Domain world-state — auto-registered via reflection over Net.Handlers's
            // Action<T> event fields. On reconnect, this re-binds to the new _router;
            // subscribers on Net.Handlers stay attached across sessions.
            int total = HandlerAutoRegistrar.Register(Net.Handlers, _router);
            if (_config.LogLifecycle)
                Debug.Log($"[Game] auto-registered {total} domain packet handlers");

            _router.OnUnknownPacket(id =>
                Debug.LogWarning($"[Game] unhandled packet id={id}"));
        }

        protected override void OnDisconnect(DisconnectReason reason)
        {
            // Clear gameplay-readiness so any in-flight Net.Senders.SendXxx (gated on
            // IsReady) stops sending while the session is down.
            IsInGame = false;
        }
    }
}
