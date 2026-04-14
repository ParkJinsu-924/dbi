#pragma once

#include "Services/GameService.h"
#include "Utils/JobQueue.h"
#include <vector>
#include <memory>
#include <thread>

class GameLoop
{
public:
	explicit GameLoop(int32 tickRate = 100);
	~GameLoop();

	GameLoop(const GameLoop&) = delete;
	GameLoop& operator=(const GameLoop&) = delete;

	void AddService(std::shared_ptr<GameService> service, float interval);
	void Start();
	void Stop();

	JobQueue& GetJobQueue() { return jobQueue_; }

private:
	struct ServiceEntry
	{
		std::shared_ptr<GameService> service;
		float interval = 0.0f;
		float accumulated = 0.0f;
	};

	void Run(std::stop_token stopToken);

	int32 tickRate_;
	std::vector<ServiceEntry> services_;
	JobQueue jobQueue_;
	std::jthread thread_;
};
