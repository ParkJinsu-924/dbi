#pragma once

#include "GameService.h"
#include "Player.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"
#include <unordered_map>
#include <atomic>
#include <string>


#define GetPlayerService() PlayerService::Instance()

class PlayerService : public TSingleton<PlayerService>, public GameService
{
public:
	void Init() override;
	void Update(float deltaTime) override;
	void Shutdown() override;

	std::shared_ptr<Player> AddPlayer(const std::string& name);
	void RemovePlayer(int32 playerId);
	std::shared_ptr<Player> FindPlayer(int32 playerId) const;
	std::vector<std::shared_ptr<Player>> GetAllPlayers() const;

private:
	Synchronized<std::unordered_map<int32, std::shared_ptr<Player>>, std::shared_mutex> players_;
	std::atomic<int32> nextPlayerId_{1};
};
