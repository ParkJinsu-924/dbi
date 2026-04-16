#pragma once

#include "Utils/CsvParser.h"


struct SpawnEntry
{
	using KeyType = int32;

	int32 tid        = 0;
	int32 zoneId     = 0;
	int32 templateId = 0;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	KeyType GetKey() const { return tid; }

	CSV_DEFINE_TYPE(SpawnEntry, tid, zoneId, templateId, x, y, z)
};
