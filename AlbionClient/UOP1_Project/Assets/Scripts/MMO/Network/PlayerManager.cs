using UnityEngine;
using System.Collections.Generic;
using Proto;

// Client-authoritative 원격 플레이어 관리.
//
// 흐름:
//   1. S_PlayerList: 입장 시 기존 플레이어 전체 목록
//   2. S_PlayerSpawn: 새 플레이어 입장
//   3. S_PlayerMove: 플레이어가 C_PlayerMove 로 보고한 권위 위치 브로드캐스트 (각 클라의 자발적 송신)
//   4. S_PlayerLeave: 나감
//
// S_UnitPositions 는 monster 전용으로 간주하고 여기서 subscribe 안 함.
public class PlayerManager : MonoBehaviour
{
    [SerializeField] private GameObject remotePlayerPrefab;

    private readonly Dictionary<int, RemotePlayerController> remotePlayers = new();

    private void Start()
    {
        var nm = NetworkManager.Instance;
        if (nm == null) return;

        nm.OnPlayerList += HandlePlayerList;
        nm.OnPlayerSpawn += HandlePlayerSpawn;
        nm.OnPlayerMove += HandlePlayerMove;
        nm.OnPlayerLeave += HandlePlayerLeave;
        nm.OnEnterGameSuccess += HandleEnterGame;
        nm.OnDisconnectedFromGame += HandleDisconnected;
    }

    private void OnDestroy()
    {
        var nm = NetworkManager.Instance;
        if (nm == null) return;

        nm.OnPlayerList -= HandlePlayerList;
        nm.OnPlayerSpawn -= HandlePlayerSpawn;
        nm.OnPlayerMove -= HandlePlayerMove;
        nm.OnPlayerLeave -= HandlePlayerLeave;
        nm.OnEnterGameSuccess -= HandleEnterGame;
        nm.OnDisconnectedFromGame -= HandleDisconnected;
    }

    private void HandleEnterGame(int localPlayerId, Vector3 spawnPos)
    {
        GameObject localPlayer = GameObject.FindGameObjectWithTag("Player");
        if (localPlayer != null && localPlayer.GetComponent<NetworkPlayerSync>() == null)
            localPlayer.AddComponent<NetworkPlayerSync>();
    }

    private void HandlePlayerList(S_PlayerList pkt)
    {
        int localId = NetworkManager.Instance.LocalPlayerId;
        foreach (var info in pkt.Players)
        {
            if (info.PlayerId == localId) continue;
            EnsureRemote(info.PlayerId, info.Name, info.Position);
        }
    }

    private void HandlePlayerSpawn(S_PlayerSpawn pkt)
    {
        if (pkt.Player == null) return;
        int localId = NetworkManager.Instance.LocalPlayerId;
        if (pkt.Player.PlayerId == localId) return;

        EnsureRemote(pkt.Player.PlayerId, pkt.Player.Name, pkt.Player.Position);
    }

    private void HandlePlayerMove(S_PlayerMove pkt)
    {
        int localId = NetworkManager.Instance.LocalPlayerId;
        if (pkt.PlayerId == localId) return;

        var pos = new Vector3(pkt.Position?.X ?? 0f, 0f, pkt.Position?.Y ?? 0f);

        if (remotePlayers.TryGetValue(pkt.PlayerId, out var ctrl))
        {
            ctrl.SetTarget(pos, pkt.Yaw);
        }
        else
        {
            // S_PlayerList / S_PlayerSpawn 보다 S_PlayerMove 가 먼저 도착한 경우 — lazy spawn.
            var placeholder = EnsureRemote(pkt.PlayerId, $"Player_{pkt.PlayerId}", null);
            placeholder.SetTarget(pos, pkt.Yaw);
        }
    }

    private void HandlePlayerLeave(S_PlayerLeave pkt)
    {
        if (remotePlayers.TryGetValue(pkt.PlayerId, out var ctrl))
        {
            if (ctrl != null) Destroy(ctrl.gameObject);
            remotePlayers.Remove(pkt.PlayerId);
            Debug.Log($"[PlayerManager] Removed remote pid={pkt.PlayerId}");
        }
    }

    private void HandleDisconnected()
    {
        foreach (var kv in remotePlayers)
            if (kv.Value != null) Destroy(kv.Value.gameObject);
        remotePlayers.Clear();
    }

    private RemotePlayerController EnsureRemote(int playerId, string playerName, Proto.Vector2 position)
    {
        if (remotePlayers.TryGetValue(playerId, out var existing))
            return existing;

        var pos = new Vector3(position?.X ?? 0f, 0f, position?.Y ?? 0f);
        GameObject go = (remotePlayerPrefab != null)
            ? Instantiate(remotePlayerPrefab, pos, Quaternion.identity)
            : CreateDefaultRemotePlayer(pos);

        go.name = $"RemotePlayer_{playerName}_{playerId}";

        var ctrl = go.GetComponent<RemotePlayerController>() ?? go.AddComponent<RemotePlayerController>();
        ctrl.PlayerId = playerId;
        ctrl.PlayerName = playerName;
        ctrl.SetTarget(pos, 0f);

        remotePlayers[playerId] = ctrl;
        Debug.Log($"[PlayerManager] Spawned remote: {playerName} (pid={playerId})");
        return ctrl;
    }

    private GameObject CreateDefaultRemotePlayer(Vector3 position)
    {
        GameObject root = new GameObject();
        root.transform.position = position;

        GameObject body = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        body.name = "Body";
        body.transform.SetParent(root.transform);
        body.transform.localPosition = new Vector3(0f, 1f, 0f);

        var col = body.GetComponent<CapsuleCollider>();
        if (col != null) Destroy(col);

        var r = body.GetComponent<Renderer>();
        if (r != null)
        {
            var mat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
            mat.color = new Color(0.2f, 0.85f, 0.3f);
            r.material = mat;
        }

        GameObject dir = GameObject.CreatePrimitive(PrimitiveType.Cube);
        dir.name = "DirectionIndicator";
        dir.transform.SetParent(root.transform);
        dir.transform.localPosition = new Vector3(0f, 1f, 0.6f);
        dir.transform.localScale = new Vector3(0.3f, 0.3f, 0.3f);
        var dirCol = dir.GetComponent<BoxCollider>();
        if (dirCol != null) Destroy(dirCol);

        var dirR = dir.GetComponent<Renderer>();
        if (dirR != null)
        {
            var dirMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
            dirMat.color = Color.white;
            dirR.material = dirMat;
        }

        return root;
    }
}
