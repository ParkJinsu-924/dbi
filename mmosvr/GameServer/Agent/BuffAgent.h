#pragma once

#include "Agent/IAgent.h"
#include "AttackTypes.h"
#include "Utils/Types.h"

#include <vector>

struct Effect;


// ===========================================================================
// BuffAgent — Unit 공통. Buff/Debuff 엔트리 관리 + CC 플래그 집계 + 스탯 modifier.
// Tick 마다 duration 감소, 만료 엔트리 제거 + S_BuffRemoved 방송.
// ApplyEffect: 즉발(Damage/Heal) 은 owner 에 바로, 지속(StatMod/CCState) 은 Add.
// ===========================================================================
class BuffAgent : public IAgent
{
public:
	struct Entry
	{
		const Effect* effect;
		long long     casterGuid;
		float         remainingDuration;
	};

	explicit BuffAgent(Unit& owner) : IAgent(owner) {}

	// IAgent ---------------------------------------------------------------
	void Tick(float dt) override;

	// Buff 추가/제거 -------------------------------------------------------

	// true = 새로 부착되었거나 기존 엔트리가 refresh 됨. false = duration<=0.
	bool Add(const Effect& e, long long casterGuid);

	// Dispel. true = 실제 제거됨.
	bool Remove(int32 eid);

	// SkillRuntime 에서 OnCast/OnHit trigger 당 호출.
	// 즉발(Damage/Heal) 은 owner_ 에 바로 반영, 지속(StatMod/CCState) 은 Add.
	void ApplyEffect(const Effect& e, long long casterGuid);

	// 호출자 편의 overload (caster 의 GUID 추출).
	void ApplyEffect(const Effect& e, const Unit& caster);

	// 조회 / 집계 ----------------------------------------------------------

	const std::vector<Entry>& GetEntries() const { return entries_; }

	uint32 GetCCFlags() const;
	void   GetStatModifier(StatType stat, float& outFlat, float& outPercent) const;
	float  EffectiveMoveSpeed(float baseSpeed) const;

	// CC 상태 질의 (원시) ---------------------------------------------------
	bool IsStunned() const      { return GetCCFlags() & static_cast<uint32>(CCFlag::Stun); }
	bool IsSilenced() const     { return GetCCFlags() & static_cast<uint32>(CCFlag::Silence); }
	bool IsRooted() const       { return GetCCFlags() & static_cast<uint32>(CCFlag::Root); }
	bool IsInvulnerable() const { return GetCCFlags() & static_cast<uint32>(CCFlag::Invulnerable); }

	// 의미적 능력 질의 (어떤 CC 가 어떤 액션을 막는가의 단일 소스) ------------
	bool CanAct()           const { return !IsStunned(); }
	bool CanMove()          const { return !(IsStunned() || IsRooted()); }
	bool CanAttack()        const { return !IsStunned(); }
	bool CanCastSkill()     const { return !(IsStunned() || IsSilenced()); }
	bool CanIgnoreDamage()  const { return IsInvulnerable(); }

private:
	std::vector<Entry> entries_;
};
