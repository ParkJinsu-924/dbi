using System;
using Proto;
using UnityEngine;

namespace MMO.Network
{
    // Owns the LoginServer TCP connection and the login-flow protocol:
    //   client → C_Login(username, password)
    //   server → S_Login(token, gameServerIp, gameServerPort)   on success
    //   server → S_Error(...)                                    on failure
    //
    // The session is one-shot: after S_Login (or S_Error) the caller closes it and
    // moves on to GameSession. There is no long-lived login channel in this protocol.
    //
    // Common machinery (TcpSession ownership, PacketRouter, Connect/Close/Dispose,
    // TrySend, OnConnected/OnDisconnected) lives on the Session base class.
    public sealed class LoginSession : Session
    {
        // Login-specific events (main thread).
        public event Action<S_Login> OnLoginAccepted;
        public event Action<S_Error> OnLoginRejected;

        protected override string LogTag => "Login";

        public LoginSession(NetworkConfig config) : base(config) { }

        protected override void RegisterHandlers()
        {
            _router.Register<S_Login>(msg =>
            {
                if (_config.LogLifecycle)
                    Debug.Log($"[Login] accepted — token={Truncate(msg.Token, 16)} game={msg.GameServerIp}:{msg.GameServerPort}");
                OnLoginAccepted?.Invoke(msg);
            });
            _router.Register<S_Error>(msg =>
            {
                Debug.LogWarning($"[Login] rejected — code={msg.Code} src={msg.SourcePacketId} detail={msg.Detail}");
                OnLoginRejected?.Invoke(msg);
            });
            _router.OnUnknownPacket(id =>
                Debug.LogWarning($"[Login] unhandled packet id={id}"));
        }

        // Connects to LoginServer + sends C_Login. Throws if TCP connect fails;
        // login result arrives asynchronously via OnLoginAccepted / OnLoginRejected.
        public void BeginLogin(string username, string password)
        {
            Connect(_config.LoginHost, _config.LoginPort);
            Net.Senders.SendLogin(username, password);
            if (_config.LogLifecycle)
                Debug.Log($"[Login] C_Login sent for {username}");
        }

        private static string Truncate(string s, int max)
            => string.IsNullOrEmpty(s) ? "" : (s.Length <= max ? s : s.Substring(0, max) + "…");
    }
}
