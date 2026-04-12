#include "pch.h"
#include "Services/PlayerService.h"


void PlayerService::Init()
{
	LOG_INFO("PlayerService initialized");
}

void PlayerService::Update(float /*deltaTime*/)
{
	// Tick-based game logic (future use)
}

void PlayerService::Shutdown()
{
	std::scoped_lock lock(mutex_);
	players_.clear();
	LOG_INFO("PlayerService shutdown");
}

int32 PlayerService::AddPlayer(const std::string& name)
{
	int32 id = nextPlayerId_.fetch_add(1);

	PlayerData data;
	data.playerId = id;
	data.name = name;
	data.position.set_x(0.0f);
	data.position.set_y(0.0f);
	data.position.set_z(0.0f);

	std::scoped_lock lock(mutex_);
	players_[id] = std::move(data);

	LOG_INFO("Player added: id=" + std::to_string(id) + " name=" + name);
	return id;
}

void PlayerService::RemovePlayer(int32 playerId)
{
	std::scoped_lock lock(mutex_);
	players_.erase(playerId);
}

PlayerData* PlayerService::FindPlayer(int32 playerId)
{
	auto it = players_.find(playerId);
	if (it == players_.end())
		return nullptr;
	return &it->second;
}

void PlayerService::MovePlayer(int32 playerId, const Proto::Vector3& newPos, float /*yaw*/)
{
	std::scoped_lock lock(mutex_);
	auto it = players_.find(playerId);
	if (it != players_.end())
	{
		it->second.position = newPos;
	}
}

std::vector<PlayerData> PlayerService::GetAllPlayers() const
{
	std::scoped_lock lock(mutex_);
	std::vector<PlayerData> result;
	result.reserve(players_.size());
	for (const auto& [id, data] : players_)
		result.push_back(data);
	return result;
}

