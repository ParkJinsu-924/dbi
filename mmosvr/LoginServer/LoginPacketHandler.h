#pragma once

#include "LoginSession.h"
#include "TokenStore.h"
#include "login.pb.h"
#include "server.pb.h"


class LoginPacketHandler
{
public:
	static void Init(TokenStore& tokenStore);

private:
	static void HandleLogin(std::shared_ptr<LoginSession> session, const Proto::C_Login& pkt);
	static void HandleValidateToken(std::shared_ptr<LoginSession> session, const Proto::SS_ValidateToken& pkt);

	static TokenStore* sTokenStore;
};
