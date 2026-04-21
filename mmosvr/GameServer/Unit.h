#pragma once

#include "GameObject.h"
#include "BuffContainer.h"
#include "Agent/IAgent.h"

#include <cassert>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

struct Effect;


// ===========================================================================
// Unit — Agent 컨테이너 + 기본 생존 상태(HP) 소유.
// 기능별 로직은 Agent (Buff / SkillCooldown / FSM / Aggro 등) 에 위임한다.
// Get<T>() 는 실패하지 않는다: 호출자가 해당 Agent 등록을 보장해야 한다.
// ===========================================================================
class Unit : public GameObject
{
public:
	Unit(GameObjectType type, Zone& zone, std::string name = "")
		: GameObject(type, zone, std::move(name)), buffs_(*this)
	{
	}

	Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
		: GameObject(type, zone, guid, std::move(name)), buffs_(*this)
	{
	}

	// --- Agent API ---------------------------------------------------------

	// ctor 에서 1 회 호출. 런타임 add/remove 는 지원하지 않는다.
	// 등록 순서가 Tick 순서이므로, 의존성 있는 Agent 는 그 의존 대상 이후에 등록한다.
	template<typename T, typename... Args>
	T& AddAgent(Args&&... args)
	{
		static_assert(std::is_base_of_v<IAgent, T>, "T must inherit IAgent");
		auto p = std::make_unique<T>(*this, std::forward<Args>(args)...);
		T& ref = *p;
		tickOrder_.push_back(p.get());
		agents_[std::type_index(typeid(T))] = std::move(p);
		return ref;
	}

	// 해당 타입의 Agent 가 반드시 등록되어 있음을 호출자가 보장해야 한다.
	// 없으면 프로그래머 버그 → assert 로 즉시 종료.
	template<typename T>
	T& Get()
	{
		auto it = agents_.find(std::type_index(typeid(T)));
		assert(it != agents_.end() && "Agent not registered on this Unit");
		return *static_cast<T*>(it->second.get());
	}

	template<typename T>
	const T& Get() const
	{
		auto it = agents_.find(std::type_index(typeid(T)));
		assert(it != agents_.end() && "Agent not registered on this Unit");
		return *static_cast<const T*>(it->second.get());
	}

	// Zone 이 매 프레임 호출. Agent 들을 등록 순서대로 Tick.
	void Update(float deltaTime) override
	{
		for (auto* a : tickOrder_)
			a->Tick(deltaTime);
	}

	// --- HP / 생존 --------------------------------------------------------

	int32 GetHp() const { return hp_; }
	void SetHp(int32 hp) { hp_ = hp; }
	int32 GetMaxHp() const { return maxHp_; }
	void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
	bool IsAlive() const { return hp_ > 0; }

	void TakeDamage(int32 amount)
	{
		if (CanIgnoreDamage()) return;
		hp_ = (std::max)(0, hp_ - amount);
	}
	void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

	// --- Buff facade (Task 2 에서 제거 예정) -------------------------------
	// 이 Task 시점엔 기존 호출처 보존을 위해 유지. Task 2 에서 전부 제거된다.
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
	BuffContainer buffs_;   // Task 2 에서 제거

private:
	// Agent 저장소. typeid(T) 로 조회.
	std::unordered_map<std::type_index, std::unique_ptr<IAgent>> agents_;
	// Tick 순서 보존용 raw pointer 벡터 (소유권은 agents_ 에).
	std::vector<IAgent*> tickOrder_;
};
