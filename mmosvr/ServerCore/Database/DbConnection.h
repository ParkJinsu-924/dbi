#pragma once


class DbConnection
{
public:
	explicit DbConnection(const std::string& connectionString);
	~DbConnection() = default;

	DbConnection(const DbConnection&) = delete;
	DbConnection& operator=(const DbConnection&) = delete;

	void Connect();
	void Reconnect();
	bool IsConnected() const;

	bool IsInUse() const { return inUse_; }
	void SetInUse(bool use) { inUse_ = use; }

	nanodbc::connection& Raw() { return conn_; }

private:
	nanodbc::connection conn_;
	std::string connectionString_;
	bool inUse_ = false;
};

