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
    public long LocalPlayerGuid { get; private set; }
    public string LocalPlayerName { get; private set; }
    public bool IsConnectedToGame => gameClient != null && gameClient.IsConnected;

    // 서버가 S_EnterGame 을 돌려줘서 Player 가 생성된 이후에만 true.
    // C_PlayerMove 같은 in-game 패킷은 이 플래그를 gate 로 사용해야 한다
    // (TCP 연결 ≠ 게임 입장 — 토큰 검증/Player 생성이 비동기).
    public bool IsInGame { get; private set; }

    public event Action OnLoginSuccess;
    public event Action<string> OnLoginFail;
    public event Action<int, UnityEngine.Vector3> OnEnterGameSuccess;
    public event Action OnDisconnectedFromGame;

    public event Action<S_PlayerList> OnPlayerList;
    public event Action<S_PlayerSpawn> OnPlayerSpawn;
    public event Action<S_PlayerLeave> OnPlayerLeave;
    public event Action<S_UnitPositions> OnUnitPositions;
    public event Action<S_PlayerMove> OnPlayerMove;      // legacy — 서버 레거시 경로 유지 시만
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
            gameRouter.Register<S_PlayerSpawn>((pkt) => OnPlayerSpawn?.Invoke(pkt));
            gameRouter.Register<S_PlayerLeave>((pkt) => OnPlayerLeave?.Invoke(pkt));
            gameRouter.Register<S_UnitPositions>((pkt) => OnUnitPositions?.Invoke(pkt));
            gameRouter.Register<S_PlayerMove>((pkt) => OnPlayerMove?.Invoke(pkt));
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
        LocalPlayerId = response.PlayerId;
        LocalPlayerGuid = response.Guid;
        IsInGame = true;

        var spawnPos = new UnityEngine.Vector3(
            response.SpawnPosition?.X ?? 0f,
            0f,
            response.SpawnPosition?.Y ?? 0f
        );

        Debug.Log($"[NetworkManager] Entered game. PlayerId: {LocalPlayerId} Guid: {LocalPlayerGuid}");
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
        IsInGame = false;
        Debug.Log("[NetworkManager] Disconnected from GameServer");
        OnDisconnectedFromGame?.Invoke();
    }

    // Deprecated: 레거시 위치 직접 송신. 서버는 여전히 받긴 하지만 S_PlayerMove 로 브로드캐스트 —
    // 신규 클라(debug_tool)는 레거시 패킷을 모름. 신규 코드는 SendMoveCommand 사용.
    public void SendMove(UnityEngine.Vector3 position, float yaw)
    {
        if (!IsConnectedToGame) return;

        var pkt = new C_PlayerMove
        {
            Position = new Proto.Vector2 { X = position.x, Y = position.z },
            Yaw = yaw
        };
        gameClient.Send(pkt);
    }

    // LoL/Albion 스타일 클릭 이동 명령. 서버가 destination 에 저장하고 tick 에서 직선으로 접근,
    // 위치는 S_UnitPositions 로 전파됨.
    public void SendMoveCommand(UnityEngine.Vector3 targetPos)
    {
        if (!IsConnectedToGame) return;

        var pkt = new C_MoveCommand
        {
            TargetPos = new Proto.Vector2 { X = targetPos.x, Y = targetPos.z }
        };
        gameClient.Send(pkt);
    }

    public void SendStopMove()
    {
        if (!IsConnectedToGame) return;
        gameClient.Send(new C_StopMove());
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
