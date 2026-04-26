using System.Collections.Generic;
using MMO.Network;
using MMO.Unit;
using Proto;
using UnityEngine;

namespace MMO.Monster
{
    // Owns the lifetime of monster visuals only — spawn / despawn dispatch
    // and prefab choice. Movement updates (S_UnitPositions) and HP changes
    // (S_UnitHp) are handled centrally by UnitRegistry once the view is
    // registered. S_MonsterMove is kept for the rare per-monster broadcast
    // (e.g. server teleport) and forwards through the same registry path.
    public sealed class MonsterManager : MonoBehaviour
    {
        [SerializeField] private MonsterView _prefab;
        [SerializeField] private Transform _container;

        private readonly Dictionary<long, MonsterView> _monsters = new();

        private void OnEnable()
        {
            Net.Handlers.OnMonsterList    += OnMonsterList;
            Net.Handlers.OnMonsterSpawn   += OnMonsterSpawn;
            Net.Handlers.OnMonsterDespawn += OnMonsterDespawn;
            Net.Handlers.OnMonsterMove    += OnMonsterMove;
        }

        private void OnDisable()
        {
            Net.Handlers.OnMonsterList    -= OnMonsterList;
            Net.Handlers.OnMonsterSpawn   -= OnMonsterSpawn;
            Net.Handlers.OnMonsterDespawn -= OnMonsterDespawn;
            Net.Handlers.OnMonsterMove    -= OnMonsterMove;

            foreach (var view in _monsters.Values)
            {
                if (view == null) continue;
                UnitRegistry.Instance?.Unregister(view.Guid);
                Destroy(view.gameObject);
            }
            _monsters.Clear();
        }

        private void OnMonsterList(S_MonsterList msg)
        {
            foreach (var info in msg.Monsters)
                Spawn(info);
        }

        private void OnMonsterSpawn(S_MonsterSpawn msg)
        {
            if (msg.Monster != null) Spawn(msg.Monster);
        }

        private void OnMonsterDespawn(S_MonsterDespawn msg)
        {
            if (!_monsters.TryGetValue(msg.Guid, out var view)) return;
            if (view != null)
            {
                UnitRegistry.Instance?.Unregister(view.Guid);
                Destroy(view.gameObject);
            }
            _monsters.Remove(msg.Guid);
        }

        private void OnMonsterMove(S_MonsterMove msg)
        {
            if (!_monsters.TryGetValue(msg.Guid, out var view) || view == null) return;
            Vector3 pos = new(msg.Position?.X ?? 0f, 0f, msg.Position?.Y ?? 0f);
            view.SetTargetPosition(pos);
        }

        private void Spawn(MonsterInfo info)
        {
            if (_prefab == null)
            {
                Debug.LogError("[MonsterManager] prefab is not assigned");
                return;
            }
            if (_monsters.ContainsKey(info.Guid)) return;

            var view = _container != null
                ? Instantiate(_prefab, _container)
                : Instantiate(_prefab);
            view.Init(info);
            _monsters[info.Guid] = view;
            UnitRegistry.Instance?.Register(view);
        }
    }
}
