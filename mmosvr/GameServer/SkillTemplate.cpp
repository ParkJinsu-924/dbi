#include "pch.h"
#include "SkillTemplate.h"
#include "SkillBehaviorRegistry.h"


void SkillTable::OnLoaded()
{
	auto& registry = SkillBehaviorRegistry::Instance();
	int missing = 0;

	for (auto& [sid, tmpl] : map_)
	{
		tmpl.behavior = registry.Create(tmpl.behaviorName);
		if (!tmpl.behavior)
		{
			LOG_ERROR(std::format(
				"SkillTable: sid={} — no behavior registered for name '{}' and no 'default' fallback",
				sid, tmpl.behaviorName));
			++missing;
		}
	}

	if (missing > 0)
	{
		// SkillBehaviorRegistry 에 'default' 가 등록되어 있으면 여기 도달 불가.
		// main::Init 순서 버그 시에만 발생 — 부팅 중단이 안전.
		throw std::runtime_error("SkillTable: behavior binding failed — check SkillBehaviorRegistry init order");
	}
}
