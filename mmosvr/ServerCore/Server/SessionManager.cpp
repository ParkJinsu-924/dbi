#include "pch.h"
#include "Server/SessionManager.h"


void SessionManager::Add(SessionPtr session)
{
	std::scoped_lock lock(mutex_);
	sessions_.insert(std::move(session));
}

void SessionManager::Remove(SessionPtr session)
{
	std::scoped_lock lock(mutex_);
	sessions_.erase(session);
}

void SessionManager::Broadcast(SendBufferChunkPtr chunk)
{
	std::scoped_lock lock(mutex_);
	for (auto& session : sessions_)
	{
		if (session->IsConnected())
		{
			session->Send(chunk);
		}
	}
}

int32 SessionManager::Count() const
{
	std::scoped_lock lock(mutex_);
	return static_cast<int32>(sessions_.size());
}

void SessionManager::Clear()
{
	std::scoped_lock lock(mutex_);
	for (auto& session : sessions_)
	{
		session->Disconnect();
	}
	sessions_.clear();
}
