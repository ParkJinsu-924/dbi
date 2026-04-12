#pragma once

#include "Utils/Types.h"


class TokenStore
{
public:
	void Store(const std::string& token, const std::string& username)
	{
		std::scoped_lock lock(mutex_);
		tokens_[token] = username;
	}

	bool Validate(const std::string& token, std::string& outUsername) const
	{
		std::scoped_lock lock(mutex_);
		auto it = tokens_.find(token);
		if (it == tokens_.end())
			return false;
		outUsername = it->second;
		return true;
	}

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::string, std::string> tokens_;
};

