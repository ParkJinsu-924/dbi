#pragma once

#include "LoginSession.h"
#include "TokenStore.h"
#include "login.pb.h"
#include "server.pb.h"
#include "common.pb.h"


class LoginPacketHandler
{
public:
	static void Init(TokenStore& tokenStore);

private:
	static Proto::ErrorCode HandleLogin(std::shared_ptr<LoginSession> session, const Proto::C_Login& pkt);
	static Proto::ErrorCode HandleValidateToken(std::shared_ptr<LoginSession> session, const Proto::SS_ValidateToken& pkt);

	static TokenStore* sTokenStore;
};
