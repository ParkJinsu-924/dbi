using System;
using Google.Protobuf;
using UnityEngine;

namespace MMO.Network
{
    // Common machinery shared by every concrete session (LoginSession, GameSession,
    // future GatewaySession, ...). Owns the TcpSession + PacketRouter pair, wires
    // recv/send threads to the main thread via MainThreadDispatcher, and exposes
    // typed primitives (Connect, Close, Dispose, TrySend) plus generic lifecycle
    // events (OnConnected, OnDisconnected).
    //
    // Adding a new session is a 4-step recipe:
    //   1. inherit Session
    //   2. override LogTag        — short string for "[Tag] ..." log lines
    //   3. override RegisterHandlers — call _router.Register<T>(handler) for each
    //                                  packet type the session listens for
    //   4. (optional) override IsReady — sends with gateOnReady=true block until
    //                                    this returns true. Default: TCP connected.
    //   5. (optional) override OnDisconnect — reset session-owned state on close.
    //
    // Senders for the new session go in PacketSenders.cs as new methods that
    // route via NetworkManager.Instance?.<TheNewSession>.
    public abstract class Session : IDisposable
    {
        protected readonly NetworkConfig _config;
        protected readonly TcpSession _tcp;
        protected readonly PacketRouter _router = new();

        public TcpSession Tcp => _tcp;
        public ConnectionState State => _tcp.State;

        // Generic lifecycle events. Always raised on the main thread (marshaled
        // through MainThreadDispatcher). Domain-specific events (OnEnteredGame,
        // OnLoginAccepted, ...) live on the subclasses.
        public event Action OnConnected;
        public event Action<DisconnectReason> OnDisconnected;

        // ── Subclass extension points ──────────────────────────────────────────
        protected abstract string LogTag { get; }
        protected abstract void RegisterHandlers();

        // Override to define what "ready to send gameplay packets" means for this
        // session. Default: TCP connected. GameSession returns IsInGame because
        // gameplay packets require S_EnterGame to have completed first.
        protected virtual bool IsReady => _tcp.IsConnected;

        // Hook for subclass-owned state to react to disconnect on the main thread.
        // Called BEFORE the public OnDisconnected event fires, so subscribers see
        // the post-reset state.
        protected virtual void OnDisconnect(DisconnectReason reason) { }

        protected Session(NetworkConfig config)
        {
            _config = config != null ? config : NetworkConfig.CreateDefault();
            _tcp = new TcpSession(_config.RecvBufferSize, _config.MaxPayloadSize, _config.SendQueueCapacity);

            // Subclass registers its packet handlers BEFORE the recv pipe goes live,
            // so the first packet received already finds a handler.
            RegisterHandlers();

            _tcp.OnConnected += () =>
            {
                if (_config.LogLifecycle) Debug.Log($"[{LogTag}] TCP connected to {_tcp.Endpoint}");
                MainThreadDispatcher.TryEnqueue(() => OnConnected?.Invoke());
            };
            _tcp.OnPacketReceived += _router.Route;
            _tcp.OnDisconnected += (reason, ex) =>
            {
                if (_config.LogLifecycle)
                    Debug.Log($"[{LogTag}] disconnected reason={reason} ex={ex?.Message}");
                MainThreadDispatcher.TryEnqueue(() =>
                {
                    OnDisconnect(reason);
                    OnDisconnected?.Invoke(reason);
                });
            };
        }

        public void Connect(string host, int port)
            => _tcp.Connect(host, port, TimeSpan.FromSeconds(_config.ConnectTimeout));

        public void Close(DisconnectReason reason = DisconnectReason.Local)
            => _tcp.Close(reason);

        public virtual void Dispose()
        {
            _tcp.Close(DisconnectReason.Local);
            _tcp.Dispose();
            _router.Clear();
        }

        // Single send primitive. Internal so PacketSenders (same assembly) can reach
        // it without exposing on the public API. Enforces the IsReady gate (when
        // gateOnReady=true), back-pressure, and packet logging in one place.
        internal bool TrySend<T>(T msg, bool gateOnReady) where T : IMessage<T>
        {
            if (gateOnReady && !IsReady)
            {
                if (_config.LogPackets)
                    Debug.LogWarning($"[{LogTag}] dropped {typeof(T).Name} — session not ready");
                return false;
            }
            bool ok = _tcp.Send(msg);
            if (!ok && _config.LogPackets)
                Debug.LogWarning($"[{LogTag}] send back-pressure dropped {typeof(T).Name}");
            else if (ok && _config.LogPackets)
                Debug.Log($"[{LogTag}] sent {typeof(T).Name}");
            return ok;
        }
    }
}
