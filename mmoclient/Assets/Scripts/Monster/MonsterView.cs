using MMO.Unit;
using Proto;
using UnityEngine;

namespace MMO.Monster
{
    // Monster. Inherits position lerp + HP storage from UnitView; adds the
    // monster-only template id (Tid). Yaw is intentionally not used — the
    // proto comment marks UnitPosition.yaw as 0 for non-player units.
    public sealed class MonsterView : UnitView
    {
        public int Tid { get; private set; }

        public void Init(MonsterInfo info)
        {
            Guid = info.Guid;
            Tid = info.Tid;
            Hp = info.Hp;
            MaxHp = info.MaxHp;
            _targetPos = new Vector3(info.Position?.X ?? 0f, 0f, info.Position?.Y ?? 0f);
            transform.position = _targetPos;
            gameObject.name = $"Monster_{Tid}_{Guid}";
        }
    }
}
