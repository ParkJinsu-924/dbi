#include "pch.h"
#include "Zone.h"
#include "GameObject.h"
#include "Player.h"
#include "Network/Session.h"


void Zone::Add(std::shared_ptr<GameObject> obj)
{
	long long guid = obj->GetGuid();
	objects_.Write([&](auto& m)
		{
			m[guid] = std::move(obj);
		});
}

void Zone::Remove(long long guid)
{
	objects_.Write([&](auto& m)
		{
			m.erase(guid);
		});
}

std::shared_ptr<GameObject> Zone::Find(long long guid) const
{
	return objects_.Read([&](const auto& m) -> std::shared_ptr<GameObject>
		{
			auto it = m.find(guid);
			if (it == m.end())
				return nullptr;
			return it->second;
		});
}

void Zone::Broadcast(SendBufferChunkPtr chunk)
{
	objects_.Read([&](const auto& m)
		{
			for (const auto& [guid, obj] : m)
			{
				if (obj->GetType() != GameObjectType::Player)
					continue;

				auto player = std::static_pointer_cast<Player>(obj);
				if (auto session = player->GetSession())
				{
					if (session->IsConnected())
						std::static_pointer_cast<Session>(session)->Send(chunk);
				}
			}
		});
}
