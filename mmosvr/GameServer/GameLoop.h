#pragma once

#include "Services/GameService.h"
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

	// interval: seconds between Update calls for this service (e.g., 0.05f = 20Hz)
	void AddService(std::shared_ptr<GameService> service, float interval);
	void Start();
	void Stop();

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
	std::jthread thread_;
};
