#pragma once

#include "TSingleton.h"
#include <chrono>


#define GetTimeManager() TimeManager::Instance()

class TimeManager : public TSingleton<TimeManager>
{
public:
	void Tick(float deltaTime)
	{
		++tick_;
		deltaTime_ = deltaTime;
		totalTime_ += deltaTime;
		nowMs_ = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
	}

	uint64_t GetTick()      const { return tick_; }
	float    GetDeltaTime() const { return deltaTime_; }
	float    GetTotalTime() const { return totalTime_; }
	int64_t  GetNowMs()     const { return nowMs_; }

private:
	uint64_t tick_      = 0;
	float    deltaTime_ = 0.0f;
	float    totalTime_ = 0.0f;
	int64_t  nowMs_     = 0;
};
