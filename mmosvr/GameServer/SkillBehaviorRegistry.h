#pragma once

#include "SkillBehavior.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>


// 이름 → Behavior 인스턴스 팩토리.
// 부팅 시 1회 Register 하고, SkillTable::OnLoaded 에서 Create 해 SkillTemplate 에 바인딩한다.
// 등록은 I/O 스레드 시작 전(main::Init) 에만 이루어지므로 락 불필요.
// shared_ptr 을 쓰는 이유: 같은 behavior 인스턴스를 여러 SkillTemplate 이 공유 가능 (상태 없음).

class SkillBehaviorRegistry
{
public:
	using Factory = std::function<std::shared_ptr<ISkillBehavior>()>;

	static SkillBehaviorRegistry& Instance()
	{
		static SkillBehaviorRegistry inst;
		return inst;
	}

	void Register(std::string name, Factory factory)
	{
		factories_[std::move(name)] = std::move(factory);
	}

	// 미등록 이름 또는 빈 문자열이면 "default" 로 폴백. "default" 도 없으면 nullptr.
	std::shared_ptr<ISkillBehavior> Create(const std::string& name) const
	{
		const std::string& key = name.empty() ? kDefault : name;
		const auto it = factories_.find(key);
		if (it == factories_.end())
		{
			const auto fallback = factories_.find(kDefault);
			return fallback != factories_.end() ? fallback->second() : nullptr;
		}
		return it->second();
	}

private:
	SkillBehaviorRegistry() = default;
	static inline const std::string kDefault = "default";
	std::unordered_map<std::string, Factory> factories_;
};
