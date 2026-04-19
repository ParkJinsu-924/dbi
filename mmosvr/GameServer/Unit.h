#pragma once

#include "GameObject.h"
#include "BuffContainer.h"

class Zone;
struct Effect;


class Unit : public GameObject
{
public:
	explicit Unit(GameObjectType type, std::string name = "")
		: GameObject(type, std::move(name)), buffs_(*this)
	{
	}

	Unit(GameObjectType type, long long guid, std::string name)
		: GameObject(type, guid, std::move(name)), buffs_(*this)
	{
	}

	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }
	int32 GetMaxHp() const { return maxHp_; }
	void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
	bool IsAlive() const { return hp_ > 0; }

	// Invulnerable 버프가 걸려있으면 데미지를 완전 무시.
	void TakeDamage(int32 amount)
	{
		if (IsInvulnerable()) return;
		hp_ = (std::max)(0, hp_ - amount);
	}
	void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

	// Zone 참조 — BuffContainer 가 S_BuffApplied/Removed 브로드캐스트 시 사용.
	// 파생 클래스 (Monster/Player) 가 override.
	virtual Zone* GetZone() const { return nullptr; }

	// ─── Effect / Buff 시스템 ────────────────────────────────────────
	// SkillRuntime 이 OnCast/OnHit 트리거의 Effect 를 이 메서드로 적용한다.
	// 즉발(Damage/Heal) 은 즉시 처리, 지속(StatMod/CCState) 는 BuffContainer 에 부착.
	void ApplyEffect(const Effect& e, Unit& caster);

	// 특정 eid 버프 외부 해제 (Dispel 등).
	bool DispelBuff(int32 eid) { return buffs_.Remove(eid); }

	// 매 프레임 호출 — 파생 클래스의 Update 시작부에서 반드시 부름.
	void TickBuffs(float dt) { buffs_.Tick(dt); }

	const BuffContainer& GetBuffs() const { return buffs_; }

	// base 값은 파생 클래스가 각자 관리. MoveToward 등에서 이 값을 쓴다.
	// 최종값 = base * (1 + %mod) + flat. 음수 방지를 위해 0 에서 clamp.
	float GetEffectiveMoveSpeed(float baseSpeed) const;

	// ─── CC 상태 (원시 플래그 조회) ────────────────────────────
	// 세부 디버깅/로깅 용도. 일반 액션 분기에서는 아래 Can* helper 를 우선 사용.
	bool IsStunned() const       { return buffs_.GetCCFlags() & static_cast<uint32>(CCFlag::Stun); }
	bool IsSilenced() const      { return buffs_.GetCCFlags() & static_cast<uint32>(CCFlag::Silence); }
	bool IsRooted() const        { return buffs_.GetCCFlags() & static_cast<uint32>(CCFlag::Root); }
	bool IsInvulnerable() const  { return buffs_.GetCCFlags() & static_cast<uint32>(CCFlag::Invulnerable); }

	// ─── 의미적 능력 체크 — "어떤 CC 가 어떤 액션을 막는가" 정책 단일 소스 ───
	// 호출자(핸들러/AI/Update)는 원시 CC 플래그를 직접 OR 하지 않고 이 helper 를 사용한다.
	// 새 CC 가 추가되어도 여기만 수정하면 모든 호출 지점에 자동 반영.
	bool CanAct()       const { return !IsStunned(); }                      // 총체적 행동 가능 여부 (Monster FSM 등)
	bool CanMove()      const { return !(IsStunned() || IsRooted()); }      // 이동 가능
	bool CanAttack()    const { return !IsStunned(); }                      // 평타 (Silence 는 평타 허용)
	bool CanCastSkill() const { return !(IsStunned() || IsSilenced()); }    // 스킬 시전

protected:
	int32 hp_ = 100;
	int32 maxHp_ = 100;
	BuffContainer buffs_;
};
