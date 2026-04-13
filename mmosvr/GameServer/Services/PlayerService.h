#pragma once

#include "GameService.h"
#include "common.pb.h"
#include "Utils/Synchronized.h"
#include <unordered_map>
#include <atomic>
#include <string>


struct PlayerData
{
	int32 playerId = 0;
	std::string name;
	Proto::Vector3 position;
};

class PlayerService : public GameService
{
public:
	void Init() override;
	void Update(float deltaTime) override;
	void Shutdown() override;

	int32 AddPlayer(const std::string& name);
	void RemovePlayer(int32 playerId);

	void MovePlayer(int32 playerId, const Proto::Vector3& newPos, float yaw);
	std::vector<PlayerData> GetAllPlayers() const;

private:
	Synchronized<std::unordered_map<int32, PlayerData>, std::shared_mutex> players_;
	std::atomic<int32> nextPlayerId_{1};
};
