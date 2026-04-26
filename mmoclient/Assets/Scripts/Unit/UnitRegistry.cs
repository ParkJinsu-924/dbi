using System.Collections.Generic;
using MMO.Network;
using Proto;
using UnityEngine;

namespace MMO.Unit
{
    // Single source of truth for "guid -> UnitView" lookup. One instance per
    // scene. Per-type managers (RemotePlayerManager, MonsterManager, ...) own
    // spawn / despawn (deciding which prefab) and call Register / Unregister
    // here; cross-cutting events that key off `guid` (S_UnitPositions, S_UnitHp,
    // and later S_BuffApplied / S_BuffRemoved / S_SkillHit) are handled here so
    // each new unit type does not need its own subscription.
    public sealed class UnitRegistry : MonoBehaviour
    {
        public static UnitRegistry Instance { get; private set; }

        private readonly Dictionary<long, UnitView> _units = new();

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
        }

        private void OnDestroy()
        {
            if (Instance == this) Instance = null;
        }

        private void OnEnable()
        {
            Net.Handlers.OnUnitPositions += OnUnitPositions;
            Net.Handlers.OnUnitHp        += OnUnitHp;
        }

        private void OnDisable()
        {
            Net.Handlers.OnUnitPositions -= OnUnitPositions;
            Net.Handlers.OnUnitHp        -= OnUnitHp;
        }

        public void Register(UnitView view)
        {
            if (view == null) return;
            _units[view.Guid] = view;
        }

        public void Unregister(long guid) => _units.Remove(guid);

        public bool TryGet(long guid, out UnitView view) => _units.TryGetValue(guid, out view);

        private void OnUnitPositions(S_UnitPositions msg)
        {
            foreach (var u in msg.Units)
            {
                if (!_units.TryGetValue(u.Guid, out var view) || view == null) continue;
                Vector3 pos = new(u.Position?.X ?? 0f, 0f, u.Position?.Y ?? 0f);
                view.SetTargetPosition(pos);
                view.SetTargetYaw(u.Yaw);
            }
        }

        private void OnUnitHp(S_UnitHp msg)
        {
            if (_units.TryGetValue(msg.Guid, out var view) && view != null)
                view.SetHp(msg.Hp, msg.MaxHp);
        }
    }
}
