using UnityEngine;
using System.Collections.Generic;
using Proto;

public class PlayerManager : MonoBehaviour
{
    [SerializeField] private GameObject remotePlayerPrefab;

    private readonly Dictionary<int, RemotePlayerController> remotePlayers = new Dictionary<int, RemotePlayerController>();

    private void Start()
    {
        var nm = NetworkManager.Instance;
        if (nm == null) return;

        nm.OnPlayerList += HandlePlayerList;
        nm.OnPlayerMove += HandlePlayerMove;
        nm.OnPlayerLeave += HandlePlayerLeave;
        nm.OnEnterGameSuccess += HandleEnterGame;
        nm.OnDisconnectedFromGame += HandleDisconnected;
    }

    private void HandleEnterGame(int localPlayerId, UnityEngine.Vector3 spawnPos)
    {
        // Attach NetworkPlayerSync to local player
        GameObject localPlayer = GameObject.FindGameObjectWithTag("Player");
        if (localPlayer != null)
        {
            var sync = localPlayer.GetComponent<NetworkPlayerSync>();
            if (sync == null)
                sync = localPlayer.AddComponent<NetworkPlayerSync>();
        }
    }

    private void HandlePlayerList(S_PlayerList pkt)
    {
        int localId = NetworkManager.Instance.LocalPlayerId;

        HashSet<int> receivedIds = new HashSet<int>();

        foreach (var playerInfo in pkt.Players)
        {
            int id = playerInfo.PlayerId;
            if (id == localId) continue;

            receivedIds.Add(id);

            var pos = new UnityEngine.Vector3(
                playerInfo.Position?.X ?? 0f,
                playerInfo.Position?.Y ?? 0f,
                playerInfo.Position?.Z ?? 0f
            );

            if (!remotePlayers.ContainsKey(id))
            {
                SpawnRemotePlayer(id, playerInfo.Name, pos);
            }
            else
            {
                remotePlayers[id].SetTarget(pos, 0f);
            }
        }

        // Remove players no longer in the list
        var toRemove = new List<int>();
        foreach (var kvp in remotePlayers)
        {
            if (!receivedIds.Contains(kvp.Key))
                toRemove.Add(kvp.Key);
        }
        foreach (int id in toRemove)
            RemoveRemotePlayer(id);
    }

    private void HandlePlayerMove(S_PlayerMove pkt)
    {
        int localId = NetworkManager.Instance.LocalPlayerId;
        if (pkt.PlayerId == localId) return;

        var pos = new UnityEngine.Vector3(
            pkt.Position?.X ?? 0f,
            pkt.Position?.Y ?? 0f,
            pkt.Position?.Z ?? 0f
        );

        if (remotePlayers.TryGetValue(pkt.PlayerId, out var remote))
        {
            remote.SetTarget(pos, pkt.Yaw);
        }
        else
        {
            // Player not yet known, spawn them
            SpawnRemotePlayer(pkt.PlayerId, "Player_" + pkt.PlayerId, pos);
            remotePlayers[pkt.PlayerId].SetTarget(pos, pkt.Yaw);
        }
    }

    private void HandlePlayerLeave(S_PlayerLeave pkt)
    {
        RemoveRemotePlayer(pkt.PlayerId);
    }

    private void HandleDisconnected()
    {
        // Remove all remote players
        foreach (var kvp in remotePlayers)
        {
            if (kvp.Value != null)
                Destroy(kvp.Value.gameObject);
        }
        remotePlayers.Clear();
    }

    private void SpawnRemotePlayer(int playerId, string playerName, UnityEngine.Vector3 position)
    {
        if (remotePlayers.ContainsKey(playerId)) return;

        GameObject go;
        if (remotePlayerPrefab != null)
        {
            go = Instantiate(remotePlayerPrefab, position, Quaternion.identity);
        }
        else
        {
            // Fallback: create a green capsule
            go = CreateDefaultRemotePlayer(position);
        }

        go.name = "RemotePlayer_" + playerId;

        var controller = go.GetComponent<RemotePlayerController>();
        if (controller == null)
            controller = go.AddComponent<RemotePlayerController>();

        controller.PlayerId = playerId;
        controller.PlayerName = playerName;
        controller.SetTarget(position, 0f);

        remotePlayers[playerId] = controller;

        Debug.Log("[PlayerManager] Spawned remote player: " + playerName + " (id=" + playerId + ")");
    }

    private void RemoveRemotePlayer(int playerId)
    {
        if (remotePlayers.TryGetValue(playerId, out var remote))
        {
            Debug.Log("[PlayerManager] Removed remote player id=" + playerId);
            if (remote != null)
                Destroy(remote.gameObject);
            remotePlayers.Remove(playerId);
        }
    }

    private GameObject CreateDefaultRemotePlayer(UnityEngine.Vector3 position)
    {
        GameObject root = new GameObject();
        root.transform.position = position;

        GameObject body = GameObject.CreatePrimitive(PrimitiveType.Capsule);
        body.name = "Body";
        body.transform.SetParent(root.transform);
        body.transform.localPosition = new UnityEngine.Vector3(0f, 1f, 0f);

        // Remove collider (remote players don't need physics)
        var collider = body.GetComponent<CapsuleCollider>();
        if (collider != null) Destroy(collider);

        // Green material to distinguish from local player (blue)
        var renderer = body.GetComponent<Renderer>();
        if (renderer != null)
        {
            var mat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
            mat.color = new Color(0.2f, 0.85f, 0.3f);
            renderer.material = mat;
        }

        // Direction indicator
        GameObject dir = GameObject.CreatePrimitive(PrimitiveType.Cube);
        dir.name = "DirectionIndicator";
        dir.transform.SetParent(root.transform);
        dir.transform.localPosition = new UnityEngine.Vector3(0f, 1f, 0.6f);
        dir.transform.localScale = new UnityEngine.Vector3(0.3f, 0.3f, 0.3f);
        var dirCollider = dir.GetComponent<BoxCollider>();
        if (dirCollider != null) Destroy(dirCollider);

        var dirRenderer = dir.GetComponent<Renderer>();
        if (dirRenderer != null)
        {
            var dirMat = new Material(Shader.Find("Universal Render Pipeline/Lit"));
            dirMat.color = Color.white;
            dirRenderer.material = dirMat;
        }

        return root;
    }

    private void OnDestroy()
    {
        var nm = NetworkManager.Instance;
        if (nm == null) return;

        nm.OnPlayerList -= HandlePlayerList;
        nm.OnPlayerMove -= HandlePlayerMove;
        nm.OnPlayerLeave -= HandlePlayerLeave;
        nm.OnEnterGameSuccess -= HandleEnterGame;
        nm.OnDisconnectedFromGame -= HandleDisconnected;
    }
}
