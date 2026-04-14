#include "pch.h"
#include "GameLoop.h"
#include "Packet/PacketHandler.h"
#include <chrono>


GameLoop::GameLoop(int32 tickRate)
	: tickRate_(tickRate)
{
}

GameLoop::~GameLoop()
{
	Stop();
}

void GameLoop::AddService(std::shared_ptr<GameService> service, float interval)
{
	services_.push_back({ std::move(service), interval, 0.0f });
}

void GameLoop::Start()
{
	// Route all packet dispatches through our job queue
	PacketHandler::Instance().SetJobQueue(&jobQueue_);

	thread_ = std::jthread([this](std::stop_token st) { Run(st); });
	LOG_INFO("GameLoop started (" + std::to_string(tickRate_) + " Hz, "
		+ std::to_string(services_.size()) + " services)");
}

void GameLoop::Stop()
{
	if (thread_.joinable())
	{
		thread_.request_stop();
		thread_.join();

		// Disconnect job queue so remaining I/O dispatches don't enqueue
		PacketHandler::Instance().SetJobQueue(nullptr);

		LOG_INFO("GameLoop stopped");
	}
}

void GameLoop::Run(std::stop_token stopToken)
{
	using Clock = std::chrono::steady_clock;

	const auto tickInterval = std::chrono::milliseconds(1000 / tickRate_);
	auto lastTick = Clock::now();

	while (!stopToken.stop_requested())
	{
		const auto now = Clock::now();
		const float deltaTime = std::chrono::duration<float>(now - lastTick).count();
		lastTick = now;

		// 1. Process all queued packets from I/O threads
		jobQueue_.Flush(); // Run Packet Handler..

		// 2. Tick services at their individual intervals
		for (auto& entry : services_)
		{
			entry.accumulated += deltaTime;
			if (entry.accumulated >= entry.interval)
			{
				entry.service->Update(entry.accumulated);
				entry.accumulated = 0.0f;
			}
		}

		const auto elapsed = Clock::now() - now;
		if (elapsed < tickInterval)
		{
			std::this_thread::sleep_for(tickInterval - elapsed);
		}
	}
}
