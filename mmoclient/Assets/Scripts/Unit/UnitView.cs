using UnityEngine;

namespace MMO.Unit
{
    // Common base for any networked unit visible in the world (RemotePlayer,
    // Monster, eventually NPC/Pet). Identified by a server-issued `guid`.
    //
    // The base owns position-lerp and HP fields — anything keyed by guid in
    // the proto (S_UnitPositions, S_UnitHp, S_BuffApplied/Removed, ...).
    // Subclasses add type-specific data (PlayerName, MonsterTid, ...) and
    // override Update / SetTargetYaw when they need extra behaviour.
    public abstract class UnitView : MonoBehaviour
    {
        [SerializeField] protected float _smoothing = 12f;
        [SerializeField] protected float _snapDistance = 5f;

        public long Guid { get; protected set; }
        public int Hp { get; protected set; }
        public int MaxHp { get; protected set; }

        protected Vector3 _targetPos;

        public virtual void SetTargetPosition(Vector3 pos)
        {
            if (Vector3.Distance(pos, transform.position) > _snapDistance)
                transform.position = pos;
            _targetPos = pos;
        }

        // Default no-op — units without a meaningful facing (Monsters per the
        // proto comment) inherit this directly. PlayerView overrides.
        public virtual void SetTargetYaw(float yaw) { }

        public virtual void SetHp(int hp, int maxHp)
        {
            Hp = hp;
            MaxHp = maxHp;
        }

        protected virtual void Update()
        {
            float t = 1f - Mathf.Exp(-_smoothing * Time.deltaTime);
            transform.position = Vector3.Lerp(transform.position, _targetPos, t);
        }
    }
}
