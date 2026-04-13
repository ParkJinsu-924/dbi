#include "pch.h"
#include "Server/SessionManager.h"


void SessionManager::Add(SessionPtr session)
{
	sessions_.Write([&](auto& set)
		{
			set.insert(std::move(session));
		});
}

void SessionManager::Remove(SessionPtr session)
{
	sessions_.Write([&](auto& set)
		{
			set.erase(session);
		});
}

void SessionManager::Broadcast(SendBufferChunkPtr chunk)
{
	sessions_.Read([&](const auto& set)
		{
			for (auto& session : set)
			{
				if (session->IsConnected())
				{
					session->Send(chunk);
				}
			}
		});
}

int32 SessionManager::Count() const
{
	return sessions_.Read([](const auto& set)
		{
			return static_cast<int32>(set.size());
		});
}

void SessionManager::Clear()
{
	sessions_.Write([](auto& set)
		{
			for (auto& session : set)
			{
				session->Disconnect();
			}
			set.clear();
		});
}
