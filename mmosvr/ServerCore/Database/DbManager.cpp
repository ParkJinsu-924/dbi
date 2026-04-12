#include "pch.h"
#include "Database/DbManager.h"


DbManager::DbManager(const std::string& connectionString,
	int32 poolSize, int32 workerThreads)
	: connectionString_(connectionString)
	, poolSize_(poolSize)
	, workerThreads_(workerThreads)
	, workGuard_(std::make_unique<net::executor_work_guard<
		net::io_context::executor_type>>(dbIoc_.get_executor()))
{
}

DbManager::~DbManager()
{
	Stop();
}

void DbManager::Start()
{
	// 커넥션 풀 생성
	pool_.reserve(poolSize_);
	for (int32 i = 0; i < poolSize_; ++i)
	{
		auto conn = std::make_shared<DbConnection>(connectionString_);
		pool_.push_back(std::move(conn));
	}
	LOG_INFO(std::format("DB connection pool created: {} connections", poolSize_));

	// DB 워커 스레드 시작
	for (int32 i = 0; i < workerThreads_; ++i)
	{
		workers_.emplace_back([this]()
			{
				dbIoc_.run();
			});
	}
	LOG_INFO(std::format("DB worker threads started: {}", workerThreads_));
}

void DbManager::Stop()
{
	workGuard_.reset();       // work guard 해제 → 대기 중인 핸들러 완료 후 자연 종료
	workers_.clear();         // jthread auto-joins (모든 핸들러 완료 대기)

	std::scoped_lock lock(poolMutex_);
	pool_.clear();
	LOG_INFO("DB manager stopped");
}

std::shared_ptr<DbConnection> DbManager::Acquire()
{
	std::unique_lock lock(poolMutex_);
	poolCv_.wait(lock, [this]()
		{
			return std::any_of(pool_.begin(), pool_.end(),
				[](const auto& c) { return !c->IsInUse(); });
		});

	for (auto& conn : pool_)
	{
		if (!conn->IsInUse())
		{
			conn->SetInUse(true);

			// 끊어진 커넥션 재연결
			if (!conn->IsConnected()) [[unlikely]]
			{
				try { conn->Reconnect(); }
				catch (const std::exception& e)
				{
					LOG_ERROR(std::format("DB reconnect failed: {}", e.what()));
					conn->SetInUse(false);
					throw;
				}
			}

			return conn;
		}
	}

	// 논리적으로 도달 불가 (condition_variable이 보장)
	throw std::runtime_error("No available DB connection");
}

void DbManager::Release(std::shared_ptr<DbConnection> conn)
{
	{
		std::scoped_lock lock(poolMutex_);
		conn->SetInUse(false);
	}
	poolCv_.notify_one();
}

DbResult DbManager::ExecuteInternal(DbConnection& conn,
	const std::string& query)
{
	auto result = nanodbc::execute(conn.Raw(), query);
	return DbResult(std::move(result));
}

std::string DbManager::BuildCallSyntax(const std::string& spName, size_t paramCount)
{
	// "{CALL sp_name(?, ?, ?)}"
	std::string query = "{CALL " + spName + "(";
	for (size_t i = 0; i < paramCount; ++i)
	{
		if (i > 0) query += ", ";
		query += '?';
	}
	query += ")}";
	return query;
}

