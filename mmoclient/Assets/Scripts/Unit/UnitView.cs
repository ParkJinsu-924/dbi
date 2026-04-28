using System.Collections;
using System.Collections.Generic;
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

        // Per-skill attack clip metadata. wind-up/recovery 비율 보정에 필요한
        // 클립 길이는 clip.length 로, 임팩트 시점은 clip.events 안의
        // "OnAttackImpact" AnimationEvent 시간으로 자동 추출한다.
        // overrideImpactTime>=0 이면 그 값을 우선 사용 (event 미설정 시 fallback).
        // triggerName 빈 문자열은 기본값 "Attack" 으로 폴백.
        [System.Serializable]
        public struct AttackClipData
        {
            public int            skillId;
            public string         triggerName;
            public AnimationClip  clip;
            public float          overrideImpactTime;
        }

        [Header("Cast / Attack")]
        // Cancel 시 강제 복귀할 default state 이름. Animator Controller 의 default state
        // 와 일치해야 한다 (현재 MonsterAnimator 는 "Blend Tree").
        [SerializeField] private string _locomotionStateName = "Blend Tree";
        // 이 유닛이 사용하는 모든 wind-up 스킬의 skillId → 클립 매핑.
        // 매칭되는 skillId 가 없으면 모션이 재생되지 않고 경고 로그가 남는다 — fail-fast.
        [SerializeField] private List<AttackClipData> _attackClips;

        public long Guid { get; protected set; }
        public int Hp { get; protected set; }
        public int MaxHp { get; protected set; }

        protected Vector3 _targetPos;

        private static readonly int SpeedHash = Animator.StringToHash("Speed");
        private const string DefaultTriggerName = "Attack";
        private const string ImpactEventName    = "OnAttackImpact";

        private Dictionary<int, AttackClipData> _clipBySkill;
        private Vector3 _prevPos;
        private bool _prevPosInitialized;

        // wind-up 종료 시 speed 를 recovery 비율로 전환하는 코루틴 핸들. Cancel/재시전 시 정리.
        private Coroutine _castSpeedSwitchCo;
        // 마지막으로 발사한 trigger hash — Cancel 시 정확히 그 trigger 만 reset 한다.
        private int _lastCastTriggerHash;

        protected virtual void Awake()
        {
            if (_animator == null)
                _animator = GetComponentInChildren<Animator>();

            _clipBySkill = new Dictionary<int, AttackClipData>();
            if (_attackClips != null)
            {
                foreach (var d in _attackClips)
                    _clipBySkill[d.skillId] = d;
            }
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

        // S_SkillCastStart 수신 시 호출. 두 단계 재생:
        //   wind-up   (0 ~ castTime)         : speed = impactClipTime / castTime
        //                                      → 클립의 0 → 임팩트 프레임을 castTime 안에
        //   recovery  (castTime ~ castEndTime) : speed = (clipLength - impactClipTime) / (castEndTime-castTime)
        //                                       → 임팩트 → 클립 끝을 recovery 안에
        // 이 비율 보정 덕분에 서버 cast_time/cast_end_time 값이 어떻게 바뀌어도
        // 임팩트는 항상 castTime 시점에, 모션 종료는 항상 castEndTime 시점에 일어난다.
        public virtual void PlayCastStart(int skillId, float castTime, float castEndTime)
        {
            if (_animator == null) return;
            if (!TryResolveAttack(skillId, out var data))
            {
                Debug.LogWarning($"[UnitView] no attack clip resolved for skillId={skillId} on guid={Guid}");
                return;
            }

            float impactTime = ResolveImpactTime(data);
            float clipLength = data.clip != null ? data.clip.length : 0f;

            float windupSpeed = (castTime > 0f && impactTime > 0f)
                ? impactTime / castTime
                : 1f;
            _animator.speed = windupSpeed;

            string triggerName = string.IsNullOrEmpty(data.triggerName) ? DefaultTriggerName : data.triggerName;
            _lastCastTriggerHash = Animator.StringToHash(triggerName);
            _animator.SetTrigger(_lastCastTriggerHash);

            // recovery 단계 speed — 코루틴이 castTime 후 전환.
            float recoveryDuration = castEndTime - castTime;
            float recoveryClipLen  = clipLength - impactTime;
            float recoverySpeed    = (recoveryDuration > 0f && recoveryClipLen > 0f)
                ? recoveryClipLen / recoveryDuration
                : 1f;

            if (_castSpeedSwitchCo != null) StopCoroutine(_castSpeedSwitchCo);
            _castSpeedSwitchCo = StartCoroutine(SwitchToRecoverySpeed(castTime, recoverySpeed));
        }

        private IEnumerator SwitchToRecoverySpeed(float delay, float newSpeed)
        {
            yield return new WaitForSeconds(delay);
            if (_animator != null) _animator.speed = newSpeed;
            _castSpeedSwitchCo = null;
        }

        // S_SkillCastCancel 수신 시 호출. 진행 중인 모션을 끊고 Locomotion 으로 복귀.
        public virtual void CancelCast(int skillId)
        {
            if (_castSpeedSwitchCo != null)
            {
                StopCoroutine(_castSpeedSwitchCo);
                _castSpeedSwitchCo = null;
            }
            if (_animator == null) return;
            if (_lastCastTriggerHash != 0)
                _animator.ResetTrigger(_lastCastTriggerHash);
            _animator.speed = 1f;
            if (!string.IsNullOrEmpty(_locomotionStateName))
                _animator.Play(_locomotionStateName, 0, 0f);
        }

        // skillId → 클립 lookup. 매칭이 없거나 clip 미설정이면 false → 호출부가 경고 로그.
        // fallback default 슬롯은 두지 않는다 — 모든 wind-up 스킬은 _attackClips 에 명시 매핑.
        private bool TryResolveAttack(int skillId, out AttackClipData data)
        {
            if (_clipBySkill != null && _clipBySkill.TryGetValue(skillId, out data) && data.clip != null)
                return true;
            data = default;
            return false;
        }

        // 임팩트 시점을 (1) overrideImpactTime>=0 이면 그 값, 없으면
        // (2) clip.events 에서 functionName=="OnAttackImpact" event 의 time 으로 결정.
        // 어느 쪽도 없으면 0 — wind-up speed=1.0 으로 폴백 (모션 길이 보정 안 함).
        private static float ResolveImpactTime(AttackClipData data)
        {
            if (data.overrideImpactTime >= 0f) return data.overrideImpactTime;
            if (data.clip == null) return 0f;

            foreach (var ev in data.clip.events)
            {
                if (ev.functionName == ImpactEventName)
                    return ev.time;
            }
            return 0f;
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
