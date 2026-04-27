using System.Collections.Generic;
using MMO.Network;
using MMO.Unit;
using Proto;
using UnityEngine;

namespace MMO.Player
{
    // Owns the lifetime of remote-player visuals only. Spawn/despawn are
    // identified by player_id (the proto's player-domain key); position
    // updates flow through UnitRegistry (keyed by guid) once the view is
    // registered. This class no longer subscribes to OnUnitPositions —
    // S_PlayerMove is kept as the per-player update channel since it carries
    // yaw and is what the existing server flow drives.
    public sealed class RemotePlayerManager : MonoBehaviour
    {
        [SerializeField] private RemotePlayerView _prefab;
        [SerializeField] private Transform _container;

        private readonly Dictionary<int, RemotePlayerView> _players = new();

        private int LocalPlayerId
            => NetworkManager.Instance?.Game?.LocalPlayerId ?? -1;

        private void OnEnable()
        {
            Net.Handlers.OnPlayerList  += OnPlayerList;
            Net.Handlers.OnPlayerSpawn += OnPlayerSpawn;
            Net.Handlers.OnPlayerLeave += OnPlayerLeave;
            Net.Handlers.OnPlayerMove  += OnPlayerMove;
        }

        private void OnDisable()
        {
            Net.Handlers.OnPlayerList  -= OnPlayerList;
            Net.Handlers.OnPlayerSpawn -= OnPlayerSpawn;
            Net.Handlers.OnPlayerLeave -= OnPlayerLeave;
            Net.Handlers.OnPlayerMove  -= OnPlayerMove;

            foreach (var view in _players.Values)
            {
                if (view == null) continue;
                UnitRegistry.Instance?.Unregister(view.Guid);
                Destroy(view.gameObject);
            }
            _players.Clear();
        }

        private void OnPlayerList(S_PlayerList msg)
        {
            foreach (var info in msg.Players)
            {
                if (info.PlayerId == LocalPlayerId) continue;
                Debug.Log($" >>>>>>>>>>>>>>> {LocalPlayerId}");
                Debug.Log($"[RemotePlayerManager] Player {info.PlayerId}");
                Spawn(info);
            }
        }

        private void OnPlayerSpawn(S_PlayerSpawn msg)
        {
            if (msg.Player == null) return;
            if (msg.Player.PlayerId == LocalPlayerId) return;
            Spawn(msg.Player);
        }

        private void OnPlayerLeave(S_PlayerLeave msg)
        {
            if (!_players.TryGetValue(msg.PlayerId, out var view)) return;
            if (view != null)
            {
                UnitRegistry.Instance?.Unregister(view.Guid);
                Destroy(view.gameObject);
            }
            _players.Remove(msg.PlayerId);
        }

        private void OnPlayerMove(S_PlayerMove msg)
        {
            if (msg.PlayerId == LocalPlayerId) return;
            if (!_players.TryGetValue(msg.PlayerId, out var view) || view == null) return;
            Vector3 pos = new(msg.Position?.X ?? 0f, 0f, msg.Position?.Y ?? 0f);
            view.SetTargetPosition(pos);
            view.SetTargetYaw(msg.Yaw);
        }

        private void Spawn(PlayerInfo info)
        {
            if (_prefab == null)
            {
                Debug.LogError("[RemotePlayerManager] prefab is not assigned");
                return;
            }
            if (_players.ContainsKey(info.PlayerId)) return;

            var view = _container != null
                ? Instantiate(_prefab, _container)
                : Instantiate(_prefab);
            view.Init(info);
            _players[info.PlayerId] = view;
            UnitRegistry.Instance?.Register(view);
        }
    }
}
