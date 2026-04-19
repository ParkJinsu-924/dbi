#pragma once

#include "Utils/Types.h"
#include "AttackTypes.h"
#include <vector>

struct Effect;
class Unit;


// Unit 에 부착된 지속 Buff/Debuff 를 관리한다.
// Add : Effect 를 Buff 엔트리로 변환해 리스트에 추가하거나, 같은 eid 가 이미 있으면
//       duration 을 refresh (Phase 1 정책).
// Tick: dt 만큼 남은 시간을 감소. 만료된 엔트리는 제거하고 소유 Unit 의 Zone 으로
//       S_BuffRemoved 를 브로드캐스트한다.
// Remove: Dispel / 외부 해제. 제거되면 true 반환.
//
// BuffContainer 는 소유 Unit 과 1:1 로 묶이며, Unit::Update 에서 Tick 이 호출된다.
// Stat/CC 조회는 Unit::GetEffectiveXxx / IsStunned 등의 wrapper 를 통해 외부에 노출된다.
class BuffContainer
{
public:
	struct Entry
	{
		const Effect* effect;              // 원본 Effect (ResourceManager 가 소유)
		long long     casterGuid;
		float         remainingDuration;
	};

	explicit BuffContainer(Unit& owner) : owner_(&owner) {}

	BuffContainer(const BuffContainer&) = delete;
	BuffContainer& operator=(const BuffContainer&) = delete;

	// true = 새로 부착되었거나 기존 엔트리가 refresh 되었음 (호출측이 S_BuffApplied 방송).
	// false = duration 이 0 이하라 Buff 로 취급 안 됨.
	bool Add(const Effect& e, long long casterGuid);

	// dt 만큼 남은 시간 감소. 만료 엔트리는 제거 + S_BuffRemoved 방송.
	void Tick(float dt);

	// 외부 해제 (Dispel 등). true = 실제 제거됨.
	bool Remove(int32 eid);

	// SkillRuntime 이 OnCast/OnHit 트리거의 Effect 를 이 메서드로 적용한다.
	// 즉발(Damage/Heal) 은 owner 에 즉시 처리, 지속(StatMod/CCState) 은 Add.
	void ApplyEffect(const Effect& e, long long casterGuid);

	// 부착된 모든 Buff 의 cc_flag bitwise OR.
	uint32 GetCCFlags() const;

	// 특정 스탯에 대한 modifier 합산.
	// outFlat   : 평탄 보정 (양수/음수). 최종값 = base * (1 + outPercent) + outFlat
	// outPercent: 비율 보정 (0.3 = +30%, -0.3 = -30%). 중첩은 단순 합산.
	void GetStatModifier(StatType stat, float& outFlat, float& outPercent) const;

	// base 이동속도에 MoveSpeed 버프 적용. 최종값 = max(0, base*(1+pct)+flat).
	float EffectiveMoveSpeed(float baseSpeed) const;

	const std::vector<Entry>& GetEntries() const { return entries_; }

	// ─── CC 상태 (원시 플래그 조회) ────────────────────────────
	bool IsStunned() const      { return GetCCFlags() & static_cast<uint32>(CCFlag::Stun); }
	bool IsSilenced() const     { return GetCCFlags() & static_cast<uint32>(CCFlag::Silence); }
	bool IsRooted() const       { return GetCCFlags() & static_cast<uint32>(CCFlag::Root); }
	bool IsInvulnerable() const { return GetCCFlags() & static_cast<uint32>(CCFlag::Invulnerable); }

	// ─── 의미적 능력 체크 — "어떤 CC 가 어떤 액션을 막는가" 정책 단일 소스 ───
	// 새 CC 가 추가되어도 여기만 수정하면 모든 호출 지점에 자동 반영.
	bool CanAct()			const { return !IsStunned(); }                   // 총체적 행동 가능 여부 (Monster FSM 등)
	bool CanMove()			const { return !(IsStunned() || IsRooted()); }   // 이동 가능
	bool CanAttack()		const { return !IsStunned(); }                   // 평타 (Silence 는 평타 허용)
	bool CanCastSkill()		const { return !(IsStunned() || IsSilenced()); } // 스킬 시전
	bool CanIgnoreDamage () const { return IsInvulnerable(); }

private:
	Unit* owner_;
	std::vector<Entry> entries_;
};
