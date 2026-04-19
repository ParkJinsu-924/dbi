#pragma once

#include "ResourceManager.h"
#include "MonsterTemplate.h"


class SpawnTable;


struct SpawnEntry
{
	using KeyType = int32;
	using Table   = SpawnTable;

	int32 tid        = 0;
	int32 zoneId     = 0;
	int32 templateId = 0;
	float x = 0.0f;   // 월드 X
	float y = 0.0f;   // 월드 Y (2D 평면 2번째 축, 구 z 의미)

	KeyType GetKey() const { return tid; }

	CSV_DEFINE_TYPE(SpawnEntry, tid, zoneId, templateId, x, y)
};


class SpawnTable : public KeyedResourceTable<SpawnEntry>
{
public:
	int OnValidate() const override
	{
		int errors = 0;
		const auto* monsters = GetResourceManager().Get<MonsterTemplate>();
		if (!monsters) return 0;

		for (const auto& [k, s] : map_)
		{
			if (!monsters->Find(s.templateId))
			{
				LOG_ERROR(std::format(
					"spawn_entries: tid={} zoneId={} references non-existent monster templateId={}",
					s.tid, s.zoneId, s.templateId));
				++errors;
			}
		}
		return errors;
	}

	const char* DebugName() const override { return "spawn_entries"; }
};
