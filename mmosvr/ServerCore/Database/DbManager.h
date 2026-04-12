#pragma once

#include "Database/DbConnection.h"
#include "Database/DbResult.h"
#include "Database/SpParam.h"


class DbManager
{
public:
	DbManager(const std::string& connectionString, int32 poolSize, int32 workerThreads);
	~DbManager();

	DbManager(const DbManager&) = delete;
	DbManager& operator=(const DbManager&) = delete;

	void Start();
	void Stop();

	// ── 동기 API (호출 스레드에서 블록) ──

	template<typename... Args>
	DbResult Execute(const std::string& query, Args&&... args);

	// ── 비동기 API (DB 스레드에서 실행, 콜백은 targetIoc로 전달) ──

	template<typename Callback, typename... Args>
	void AsyncExecute(net::io_context& targetIoc,
		const std::string& query,
		Callback&& callback,
		Args&&... args);

	// ── Stored Procedure API ──

	template<typename... Args>
	DbResult CallProcedure(const std::string& spName, Args&&... args);

	template<typename Callback, typename... Args>
		requires AllInputParams<Args...>
	void AsyncCallProcedure(net::io_context& targetIoc,
		const std::string& spName,
		Callback&& callback,
		Args&&... args);

private:
	std::shared_ptr<DbConnection> Acquire();
	void Release(std::shared_ptr<DbConnection> conn);

	DbResult ExecuteInternal(DbConnection& conn,
		const std::string& query);

	static std::string BuildCallSyntax(const std::string& spName, size_t paramCount);

	// 파라미터 바인딩 헬퍼
	template<typename T>
	void BindParam(nanodbc::statement& stmt, int16 index, const T& value);

	template<typename... Args>
	void BindParams(nanodbc::statement& stmt, Args&&... args);

	// 커넥션 풀
	std::vector<std::shared_ptr<DbConnection>> pool_;
	std::mutex poolMutex_;
	std::condition_variable poolCv_;

	// DB 전용 스레드풀
	net::io_context dbIoc_;
	std::unique_ptr<net::executor_work_guard<net::io_context::executor_type>> workGuard_;
	std::vector<std::jthread> workers_;

	std::string connectionString_;
	int32 poolSize_;
	int32 workerThreads_;
};

// ── Template implementations ──

template<typename T>
void DbManager::BindParam(nanodbc::statement& stmt, int16 index, const T& value)
{
	using Decayed = std::decay_t<T>;
	if constexpr (IsOutParamType<Decayed>::value)
		stmt.bind(index, value.ptr, nanodbc::statement::PARAM_OUT);
	else if constexpr (IsInOutParamType<Decayed>::value)
		stmt.bind(index, value.ptr, nanodbc::statement::PARAM_INOUT);
	else if constexpr (std::is_same_v<Decayed, std::string>)
		stmt.bind(index, value.c_str());
	else if constexpr (std::is_same_v<Decayed, const char*>)
		stmt.bind(index, value);
	else if constexpr (std::is_integral_v<Decayed> || std::is_floating_point_v<Decayed>)
		stmt.bind(index, &value);
	else
		static_assert(!sizeof(T), "Unsupported parameter type for DB binding");
}

template<typename... Args>
void DbManager::BindParams(nanodbc::statement& stmt, Args&&... args)
{
	int16 index = 0;
	(BindParam(stmt, index++, std::forward<Args>(args)), ...);
}

template<typename... Args>
DbResult DbManager::Execute(const std::string& query, Args&&... args)
{
	auto conn = Acquire();
	try
	{
		if constexpr (sizeof...(args) == 0)
		{
			auto result = ExecuteInternal(*conn, query);
			Release(conn);
			return result;
		}
		else
		{
			nanodbc::statement stmt(conn->Raw(), query);
			BindParams(stmt, std::forward<Args>(args)...);
			auto result = nanodbc::execute(stmt);
			Release(conn);
			return DbResult(std::move(result));
		}
	}
	catch (...)
	{
		Release(conn);
		throw;
	}
}

template<typename... Args>
DbResult DbManager::CallProcedure(const std::string& spName, Args&&... args)
{
	auto query = BuildCallSyntax(spName, sizeof...(args));
	auto conn = Acquire();
	try
	{
		if constexpr (sizeof...(args) == 0)
		{
			auto result = ExecuteInternal(*conn, query);
			Release(conn);
			return result;
		}
		else
		{
			nanodbc::statement stmt(conn->Raw(), query);
			BindParams(stmt, std::forward<Args>(args)...);
			auto result = nanodbc::execute(stmt);
			Release(conn);
			return DbResult(std::move(result));
		}
	}
	catch (...)
	{
		Release(conn);
		throw;
	}
}

template<typename Callback, typename... Args>
	requires AllInputParams<Args...>
void DbManager::AsyncCallProcedure(net::io_context& targetIoc,
	const std::string& spName,
	Callback&& callback,
	Args&&... args)
{
	auto query = BuildCallSyntax(spName, sizeof...(args));
	AsyncExecute(targetIoc, query, std::forward<Callback>(callback),
		std::forward<Args>(args)...);
}

template<typename Callback, typename... Args>
void DbManager::AsyncExecute(net::io_context& targetIoc,
	const std::string& query,
	Callback&& callback,
	Args&&... args)
{
	net::post(dbIoc_,
		[this, &targetIoc, query,
		cb = std::forward<Callback>(callback),
		...capturedArgs = std::forward<Args>(args)]() mutable
		{
			std::shared_ptr<DbConnection> conn;
			try
			{
				conn = Acquire();
				DbResult result = [&]()
					{
						if constexpr (sizeof...(capturedArgs) == 0)
						{
							return ExecuteInternal(*conn, query);
						}
						else
						{
							nanodbc::statement stmt(conn->Raw(), query);
							BindParams(stmt, capturedArgs...);
							auto r = nanodbc::execute(stmt);
							return DbResult(std::move(r));
						}
					}();
				Release(conn);

				net::post(targetIoc,
					[cb = std::move(cb), result = std::move(result)]() mutable
					{
						cb(std::move(result));
					});
			}
			catch (const std::exception& e)
			{
				LOG_ERROR(std::format("AsyncExecute failed: {}", e.what()));
				if (conn)
					Release(conn);
				net::post(targetIoc,
					[cb = std::move(cb)]() mutable
					{
						cb(DbResult{});
					});
			}
		});
}

