#pragma once

#include "GameObject.h"
#include "Agent/IAgent.h"
#include "Agent/BuffAgent.h"

#include <cassert>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>


// ===========================================================================
// Unit — Agent 컨테이너 + 기본 생존 상태(HP) 소유.
// 기능별 로직(Buff / SkillCooldown / FSM / Aggro …) 은 Agent 에 위임.
// Get<T>() 는 실패하지 않는 계약: 없는 Agent 접근은 프로그래머 버그 → assert.
// ===========================================================================
class Unit : public GameObject
{
public:
	Unit(GameObjectType type, Zone& zone, std::string name = "")
		: GameObject(type, zone, std::move(name))
	{
		AddAgent<BuffAgent>();
	}

	Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
		: GameObject(type, zone, guid, std::move(name))
	{
		AddAgent<BuffAgent>();
	}

	// --- Agent API --------------------------------------------------------

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

	// Invulnerable 버프가 걸려있으면 데미지를 완전 무시.
	void TakeDamage(int32 amount)
	{
		if (Get<BuffAgent>().CanIgnoreDamage()) return;
		hp_ = (std::max)(0, hp_ - amount);
	}
	void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

protected:
	int32 hp_ = 100;
	int32 maxHp_ = 100;

private:
	std::unordered_map<std::type_index, std::unique_ptr<IAgent>> agents_;
	std::vector<IAgent*> tickOrder_;
};
