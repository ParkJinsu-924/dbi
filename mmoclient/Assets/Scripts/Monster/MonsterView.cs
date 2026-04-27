using MMO.Unit;
using Proto;
using UnityEngine;

namespace MMO.Monster
{
    // Monster. Inherits position lerp + HP storage from UnitView; adds the
    // monster-only template id (Tid). The proto marks UnitPosition.yaw as 0
    // for non-player units, so we derive facing from movement direction.
    public sealed class MonsterView : UnitView
    {
        // Below this per-frame movement, the unit is treated as idle and
        // facing is held — avoids jitter when lerp residual is sub-pixel.
        private const float FacingMinDelta = 0.001f;

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

        protected override void Update()
        {
            Vector3 before = transform.position;
            base.Update();

            Vector3 delta = transform.position - before;
            delta.y = 0f;
            if (delta.sqrMagnitude < FacingMinDelta * FacingMinDelta) return;

            Quaternion target = Quaternion.LookRotation(delta.normalized, Vector3.up);
            float t = 1f - Mathf.Exp(-_smoothing * Time.deltaTime);
            transform.rotation = Quaternion.Slerp(transform.rotation, target, t);
        }
    }
}
