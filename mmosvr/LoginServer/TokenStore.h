#pragma once

#include "Utils/Types.h"
#include "Utils/Synchronized.h"
#include "Utils/TSingleton.h"


#define GetTokenStore() TokenStore::Instance()

class TokenStore : public TSingleton<TokenStore>
{
public:
	void Store(const std::string& token, const std::string& username)
	{
		tokens_.Write([&](auto& m)
			{
				m[token] = username;
			});
	}

	bool Validate(const std::string& token, std::string& outUsername) const
	{
		return tokens_.Read([&](const auto& m)
			{
				auto it = m.find(token);
				if (it == m.end())
					return false;
				outUsername = it->second;
				return true;
			});
	}

private:
	Synchronized<std::unordered_map<std::string, std::string>, std::shared_mutex> tokens_;
};