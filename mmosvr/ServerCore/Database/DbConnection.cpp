#include "pch.h"
#include "Database/DbConnection.h"


DbConnection::DbConnection(const std::string& connectionString)
	: connectionString_(connectionString)
{
	Connect();
}

void DbConnection::Connect()
{
	try
	{
		conn_ = nanodbc::connection(connectionString_);
		LOG_INFO("DB connection established");
	}
	catch (const nanodbc::database_error& e)
	{
		LOG_ERROR(std::format("DB connection failed: {}", e.what()));
		throw;
	}
}

void DbConnection::Reconnect()
{
	LOG_WARN("Attempting DB reconnection...");
	conn_ = nanodbc::connection();
	Connect();
}

bool DbConnection::IsConnected() const
{
	return conn_.connected();
}

