#pragma once

#include "GameObject.h"
#include "BuffContainer.h"

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
		if (CanIgnoreDamage()) return;
		hp_ = (std::max)(0, hp_ - amount);
	}
	void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

	// ─── Buff 시스템 facade (실체는 BuffContainer) ─────────────────
	// 모든 메서드는 buffs_ 로 위임. 버프 관련 정책/상태는 BuffContainer.h/cpp 참조.
	void ApplyEffect(const Effect& e, const Unit& caster) { buffs_.ApplyEffect(e, caster.GetGuid()); }
	bool DispelBuff(const int32 eid)                      { return buffs_.Remove(eid); }
	void TickBuffs(const float dt)                        { buffs_.Tick(dt); }

	float GetEffectiveMoveSpeed(float baseSpeed) const { return buffs_.EffectiveMoveSpeed(baseSpeed); }

	bool CanAct()       const { return buffs_.CanAct(); }
	bool CanMove()      const { return buffs_.CanMove(); }
	bool CanAttack()    const { return buffs_.CanAttack(); }
	bool CanCastSkill() const { return buffs_.CanCastSkill(); }
	bool CanIgnoreDamage() const { return buffs_.CanIgnoreDamage(); }

protected:
	int32 hp_ = 100;
	int32 maxHp_ = 100;
	BuffContainer buffs_;
};
