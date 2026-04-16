#pragma once

#include "../Player.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"
#include <unordered_map>
#include <atomic>
#include <string>


#define GetPlayerManager() PlayerManager::Instance()

class PlayerManager : public TSingleton<PlayerManager>
{
public:
	std::shared_ptr<Player> AddPlayer(const std::string& name);
	void RemovePlayer(int32 playerId);
	std::shared_ptr<Player> FindPlayer(int32 playerId) const;
	std::vector<std::shared_ptr<Player>> GetAllPlayers() const;

private:
	Synchronized<std::unordered_map<int32, std::shared_ptr<Player>>, std::shared_mutex> players_;
	std::atomic<int32> nextPlayerId_{1};
};
