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
        [SerializeField] protected Animator _animator;

        public long Guid { get; protected set; }
        public int Hp { get; protected set; }
        public int MaxHp { get; protected set; }

        protected Vector3 _targetPos;

        private static readonly int SpeedHash = Animator.StringToHash("Speed");
        private Vector3 _prevPos;
        private bool _prevPosInitialized;

        protected virtual void Awake()
        {
            if (_animator == null)
                _animator = GetComponentInChildren<Animator>();
        }

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

            UpdateAnimatorSpeed();
        }

        private void UpdateAnimatorSpeed()
        {
            if (!_prevPosInitialized)
            {
                _prevPos = transform.position;
                _prevPosInitialized = true;
                return;
            }

            if (_animator != null && Time.deltaTime > 0f)
            {
                float speed = (transform.position - _prevPos).magnitude / Time.deltaTime;
                _animator.SetFloat(SpeedHash, speed, 0.1f, Time.deltaTime);
            }
            _prevPos = transform.position;
        }
    }
}
