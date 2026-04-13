using UnityEngine;
using System;
using Proto;

public class NetworkManager : MonoBehaviour
{
    static public NetworkManager Instance { get; private set; }

    [Header("Login Server")]
    [SerializeField] private string loginServerIp = "127.0.0.1";
    [SerializeField] private int loginServerPort = 9999;

    private NetworkClient loginClient;
    private NetworkClient gameClient;
    private PacketRouter loginRouter;
    private PacketRouter gameRouter;

    private string token;
    private string gameServerIp;
    private int gameServerPort;

    public int LocalPlayerId { get; private set; }
    public string LocalPlayerName { get; private set; }
    public bool IsConnectedToGame => gameClient != null && gameClient.IsConnected;

    public event Action OnLoginSuccess;
    public event Action<string> OnLoginFail;
    public event Action<int, UnityEngine.Vector3> OnEnterGameSuccess;
    public event Action OnDisconnectedFromGame;

    public event Action<S_PlayerList> OnPlayerList;
    public event Action<S_PlayerMove> OnPlayerMove;
    public event Action<S_PlayerLeave> OnPlayerLeave;
    public event Action<S_Chat> OnChat;
    public event Action<S_MoveCorrection> OnMoveCorrection;
    public event Action<S_Error> OnServerError;

    private void Awake()
    {
        if (Instance != null && Instance != this)
        {
            Destroy(gameObject);
            return;
        }
        Instance = this;
        DontDestroyOnLoad(gameObject);
        _ = MainThreadDispatcher.Instance;
    }

    public void Login(string username, string password)
    {
        try
        {
            LocalPlayerName = username;

            loginClient = new NetworkClient();
            loginRouter = new PacketRouter();

            loginRouter.Register<S_Login>(OnLoginResponse);
            loginRouter.Register<S_Error>(OnLoginServerError);

            loginClient.OnPacketReceived += loginRouter.Route;
            loginClient.OnDisconnected += OnLoginDisconnected;

            loginClient.Connect(loginServerIp, loginServerPort);

            var loginPkt = new C_Login
            {
                Username = username,
                Password = password
            };
            loginClient.Send(loginPkt);

            Debug.Log("[NetworkManager] Login request sent for: " + username);
        }
        catch (Exception e)
        {
            Debug.LogError("[NetworkManager] Login connection failed: " + e.Message);
            OnLoginFail?.Invoke("Connection failed: " + e.Message);
        }
    }

    private void OnLoginResponse(S_Login response)
    {
        // Receipt of S_Login implies success. Failure arrives as S_Error.
        token = response.Token;
        gameServerIp = string.IsNullOrEmpty(response.GameServerIp) ? loginServerIp : response.GameServerIp;
        gameServerPort = response.GameServerPort > 0 ? response.GameServerPort : 7777;

        Debug.Log("[NetworkManager] Login success. Token: " + token);

        loginClient?.Disconnect();
        loginClient = null;

        OnLoginSuccess?.Invoke();
        ConnectToGameServer();
    }

    private void OnLoginServerError(S_Error error)
    {
        string msg = "code=" + error.Code + " detail=" + error.Detail;
        Debug.LogError("[NetworkManager] LoginServer error: " + msg);
        loginClient?.Disconnect();
        loginClient = null;
        OnLoginFail?.Invoke(msg);
    }

    private void OnLoginDisconnected()
    {
        Debug.Log("[NetworkManager] Disconnected from LoginServer");
    }

    private void ConnectToGameServer()
    {
        try
        {
            gameClient = new NetworkClient();
            gameRouter = new PacketRouter();

            gameRouter.Register<S_EnterGame>(OnEnterGameResponse);
            gameRouter.Register<S_PlayerList>((pkt) => OnPlayerList?.Invoke(pkt));
            gameRouter.Register<S_PlayerMove>((pkt) => OnPlayerMove?.Invoke(pkt));
            gameRouter.Register<S_PlayerLeave>((pkt) => OnPlayerLeave?.Invoke(pkt));
            gameRouter.Register<S_Chat>((pkt) => OnChat?.Invoke(pkt));
            gameRouter.Register<S_MoveCorrection>((pkt) => OnMoveCorrection?.Invoke(pkt));
            gameRouter.Register<S_Error>(OnGameServerError);

            gameClient.OnPacketReceived += gameRouter.Route;
            gameClient.OnDisconnected += OnGameDisconnected;

            gameClient.Connect(gameServerIp, gameServerPort);

            var enterPkt = new C_EnterGame { Token = token };
            gameClient.Send(enterPkt);

            Debug.Log("[NetworkManager] Connecting to GameServer: " + gameServerIp + ":" + gameServerPort);
        }
        catch (Exception e)
        {
            Debug.LogError("[NetworkManager] GameServer connection failed: " + e.Message);
        }
    }

    private void OnEnterGameResponse(S_EnterGame response)
    {
        // Receipt of S_EnterGame implies success. Failure arrives as S_Error.
        LocalPlayerId = response.PlayerId;
        var spawnPos = new UnityEngine.Vector3(
            response.SpawnPosition?.X ?? 0f,
            response.SpawnPosition?.Y ?? 0f,
            response.SpawnPosition?.Z ?? 0f
        );

        Debug.Log("[NetworkManager] Entered game. PlayerId: " + LocalPlayerId);
        OnEnterGameSuccess?.Invoke(LocalPlayerId, spawnPos);
    }

    private void OnGameServerError(S_Error error)
    {
        // Generic server error. UI/gameplay layers can subscribe to OnServerError for handling.
        Debug.LogError("[NetworkManager] GameServer error: source_packet_id=" + error.SourcePacketId
            + " code=" + error.Code + " detail=" + error.Detail);
        OnServerError?.Invoke(error);
    }

    private void OnGameDisconnected()
    {
        Debug.Log("[NetworkManager] Disconnected from GameServer");
        OnDisconnectedFromGame?.Invoke();
    }

    public void SendMove(UnityEngine.Vector3 position, float yaw)
    {
        if (!IsConnectedToGame) return;

        var pkt = new C_PlayerMove
        {
            Position = new Proto.Vector3
            {
                X = position.x,
                Y = position.y,
                Z = position.z
            },
            Yaw = yaw
        };
        gameClient.Send(pkt);
    }

    public void SendChat(string message)
    {
        if (!IsConnectedToGame) return;

        var pkt = new C_Chat { Message = message };
        gameClient.Send(pkt);
    }

    private void OnApplicationQuit()
    {
        Shutdown();
    }

    private void OnDestroy()
    {
        if (Instance == this)
        {
            Shutdown();
            Instance = null;
        }
    }

    private void Shutdown()
    {
        loginClient?.Disconnect();
        gameClient?.Disconnect();
        loginClient = null;
        gameClient = null;
    }
}
