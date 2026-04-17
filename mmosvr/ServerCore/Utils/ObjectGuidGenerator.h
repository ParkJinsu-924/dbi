#pragma once

#include "Utils/TSingleton.h"
#include <atomic>


#define GetObjectGuidGenerator() ObjectGuidGenerator::Instance()

class ObjectGuidGenerator : public TSingleton<ObjectGuidGenerator>
{
public:
	long long Generate()
	{
		return nextGuid_.fetch_add(1, std::memory_order_relaxed);
	}

private:
	std::atomic<long long> nextGuid_{1};  // 0은 invalid sentinel로 예약
};
