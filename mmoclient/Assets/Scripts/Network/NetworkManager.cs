using System;
using Proto;
using UnityEngine;

namespace MMO.Network
{
    // Façade and lifecycle owner for the two MMO sessions (Login + Game).
    // Place a single instance in the bootstrap scene; access via NetworkManager.Instance.
    //
    // High-level flow (mirrors the C++ server architecture):
    //   1. Login(username, password)
    //         → connects to LoginServer
    //         → sends C_Login
    //         → receives S_Login(token, gameIp, gamePort)  OR  S_Error
    //   2. on S_Login: closes LoginSession, opens GameSession, sends C_EnterGame(token)
    //   3. on S_EnterGame: IsInGame becomes true; gameplay packets are now permitted.
    //
    // The manager is a thin orchestrator. World/UI code should subscribe to the
    // strongly-typed events on Game (NetworkManager.Instance.Game.OnPlayerSpawn += ...)
    // rather than reaching into the TcpSession directly.
    public sealed class NetworkManager : MonoBehaviour
    {
        public static NetworkManager Instance { get; private set; }

        [Tooltip("Per-environment endpoints. Leave empty to use a runtime default (127.0.0.1).")]
        [SerializeField] private NetworkConfig _config;

        private LoginSession _login;
        private GameSession _game;

        private string _pendingUsername;
        private float _loginIssuedAt;

        public NetworkConfig Config => _config;
        public GameSession Game => _game;
        public LoginSession Login => _login;

        public bool IsLoginInFlight => _login != null && _login.State != ConnectionState.Disconnected;
        public bool IsConnectedToGame => _game != null && _game.State == ConnectionState.Connected;
        public bool IsInGame => _game != null && _game.IsInGame;
        public string LocalPlayerName { get; private set; }

        // ── Manager-level events (always main-thread) ──────────────────────────
        public event Action OnLoginSucceeded;
        public event Action<S_Error> OnLoginFailed;
        public event Action<string> OnLoginConnectionFailed;   // TCP-level failure (refused / timeout)
        public event Action<S_EnterGame> OnEnteredGame;
        public event Action<DisconnectReason> OnGameDisconnected;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);

            // Force the dispatcher to exist on the main thread before any background
            // thread tries to enqueue. Touching the property triggers lazy creation.
            _ = MainThreadDispatcher.Instance;

            if (_config == null)
                _config = NetworkConfig.CreateDefault();
        }

        private void Update()
        {
            // Soft timeout for the login round-trip. The TCP connect itself has its own
            // timeout inside TcpSession.Connect; this catches the case where the server
            // accepted us but never replied with S_Login.
            if (_login != null && _login.State == ConnectionState.Connected)
            {
                if (Time.realtimeSinceStartup - _loginIssuedAt > _config.LoginResponseTimeout)
                {
                    Debug.LogWarning("[NetworkManager] login response timed out");
                    var failed = OnLoginConnectionFailed;
                    DisposeLogin();
                    failed?.Invoke("login timed out");
                }
            }
        }

        public void DoLogin(string username, string password)
        {
            if (IsLoginInFlight || IsConnectedToGame)
            {
                Debug.LogWarning("[NetworkManager] login already in progress / already in game");
                return;
            }

            LocalPlayerName = username;
            _pendingUsername = username;

            _login = new LoginSession(_config);
            _login.OnLoginAccepted += HandleLoginAccepted;
            _login.OnLoginRejected += HandleLoginRejected;
            _login.OnDisconnected += HandleLoginDisconnected;

            try
            {
                _login.BeginLogin(username, password);
                _loginIssuedAt = Time.realtimeSinceStartup;
            }
            catch (Exception ex)
            {
                Debug.LogError($"[NetworkManager] login connect failed: {ex.Message}");
                DisposeLogin();
                OnLoginConnectionFailed?.Invoke(ex.Message);
            }
        }

        public void Disconnect()
        {
            DisposeLogin();
            DisposeGame();
        }

        private void HandleLoginAccepted(S_Login s)
        {
            string host = string.IsNullOrEmpty(s.GameServerIp) ? _config.GameHostFallback : s.GameServerIp;
            int port = s.GameServerPort > 0 ? s.GameServerPort : _config.GamePortFallback;
            string token = s.Token;

            // Login channel is one-shot — close it before opening the game channel so we
            // don't keep an idle TCP socket hanging around.
            DisposeLogin();
            OnLoginSucceeded?.Invoke();

            ConnectGame(host, port, token);
        }

        private void HandleLoginRejected(S_Error err)
        {
            DisposeLogin();
            OnLoginFailed?.Invoke(err);
        }

        private void HandleLoginDisconnected(DisconnectReason reason)
        {
            // If the server hung up before we received S_Login or S_Error, treat as a
            // connection failure so the UI can retry / show an error.
            if (_login != null)
            {
                DisposeLogin();
                OnLoginConnectionFailed?.Invoke($"login disconnected ({reason})");
            }
        }

        private void ConnectGame(string host, int port, string token)
        {
            _game = new GameSession(_config);
            _game.OnEnteredGame += HandleEnteredGame;
            _game.OnDisconnected += HandleGameDisconnected;

            try
            {
                _game.Connect(host, port);
                Net.Senders.SendEnterGame(token);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[NetworkManager] game connect failed: {ex.Message}");
                DisposeGame();
                OnGameDisconnected?.Invoke(DisconnectReason.ConnectFailed);
            }
        }

        private void HandleEnteredGame(S_EnterGame s) => OnEnteredGame?.Invoke(s);

        private void HandleGameDisconnected(DisconnectReason reason)
        {
            OnGameDisconnected?.Invoke(reason);
            // Keep _game alive so callers can inspect last state if needed; explicit
            // Disconnect() or new login will tear it down.
        }

        private void DisposeLogin()
        {
            if (_login == null) return;
            _login.OnLoginAccepted -= HandleLoginAccepted;
            _login.OnLoginRejected -= HandleLoginRejected;
            _login.OnDisconnected -= HandleLoginDisconnected;
            _login.Dispose();
            _login = null;
        }

        private void DisposeGame()
        {
            if (_game == null) return;
            _game.OnEnteredGame -= HandleEnteredGame;
            _game.OnDisconnected -= HandleGameDisconnected;
            _game.Dispose();
            _game = null;
        }

        private void OnApplicationQuit() => Disconnect();
        private void OnDestroy()
        {
            if (Instance == this)
            {
                Disconnect();
                Instance = null;
            }
        }
    }
}
