# DB Manager Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** ServerCore에 ODBC 기반 DB 접근 계층(커넥션 풀 + 동기/비동기 API)을 추가한다.

**Architecture:** nanodbc를 사용한 ODBC 래퍼로 MSSQL/MySQL 모두 지원. DbManager가 커넥션 풀과 DB 전용 스레드풀을 관리하며, 동기 `Execute()`와 비동기 `AsyncExecute()` API를 제공한다. 비동기 콜백은 `post(targetIoc)`로 I/O 스레드에 전달된다.

**Tech Stack:** C++20, nanodbc (vcpkg), Boost.Asio, ODBC

**Spec:** `docs/superpowers/specs/2026-03-12-db-manager-design.md`

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `ServerCore/Database/DbConnection.h` | nanodbc 커넥션 래퍼 (연결/재연결/상태) |
| Create | `ServerCore/Database/DbConnection.cpp` | DbConnection 구현 |
| Create | `ServerCore/Database/DbResult.h` | 쿼리 결과 래퍼 (행/컬럼 접근) |
| Create | `ServerCore/Database/DbManager.h` | 커넥션 풀 + 동기/비동기 API 선언 |
| Create | `ServerCore/Database/DbManager.cpp` | DbManager 구현 |
| Modify | `ServerCore/pch.h` | `<nanodbc/nanodbc.h>` 추가 |
| Modify | `ServerCore/ServerCore.vcxproj` | Database 소스/헤더 파일 등록 |

---

## Chunk 1: 환경 설정 + DbConnection

### Task 1: vcpkg에 nanodbc 설치

- [ ] **Step 1: nanodbc 패키지 설치**

```bash
C:\vcpkg\vcpkg\vcpkg install nanodbc:x64-windows
```

Expected: `nanodbc:x64-windows` 패키지 설치 완료.

- [ ] **Step 2: 설치 확인**

```bash
C:\vcpkg\vcpkg\vcpkg list | grep nanodbc
```

Expected: `nanodbc:x64-windows` 출력.

---

### Task 2: pch.h에 nanodbc 헤더 추가

**Files:**
- Modify: `ServerCore/pch.h:31` (Boost.Asio 아래)

- [ ] **Step 1: nanodbc include 추가**

`ServerCore/pch.h`의 Boost.Asio 아래에 추가:

```cpp
// Database (ODBC)
#include <nanodbc/nanodbc.h>
```

- [ ] **Step 2: 빌드 확인**

```bash
msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m /t:ServerCore
```

Expected: 빌드 성공 (0 errors).

- [ ] **Step 3: Commit**

```bash
git add ServerCore/pch.h
git commit -m "chore: add nanodbc to pch.h for DB support"
```

---

### Task 3: DbConnection 클래스 구현

**Files:**
- Create: `ServerCore/Database/DbConnection.h`
- Create: `ServerCore/Database/DbConnection.cpp`

- [ ] **Step 1: DbConnection.h 작성**

```cpp
#pragma once

namespace mmo
{

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

} // namespace mmo
```

- [ ] **Step 2: DbConnection.cpp 작성**

```cpp
#include "pch.h"
#include "Database/DbConnection.h"

namespace mmo
{

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

} // namespace mmo
```

- [ ] **Step 3: Commit**

```bash
git add ServerCore/Database/DbConnection.h ServerCore/Database/DbConnection.cpp
git commit -m "feat: add DbConnection class wrapping nanodbc"
```

---

## Chunk 2: DbResult

### Task 4: DbResult 클래스 구현

**Files:**
- Create: `ServerCore/Database/DbResult.h`

- [ ] **Step 1: DbResult.h 작성 (header-only)**

```cpp
#pragma once

namespace mmo
{

	class DbResult
	{
	public:
		DbResult() = default;

		// SELECT 쿼리 결과용
		explicit DbResult(nanodbc::result result)
			: result_(std::move(result)), valid_(true)
			, affectedRows_(result_.affected_rows())
		{
		}

		// INSERT/UPDATE/DELETE 결과용
		explicit DbResult(int32 affectedRows)
			: affectedRows_(affectedRows)
		{
		}

		bool Next()
		{
			if (!valid_) return false;
			return result_.next();
		}

		// ── 인덱스 기반 접근 ──

		int32 GetInt32(int16 col) const { return result_.get<int32>(col); }
		int64 GetInt64(int16 col) const { return result_.get<int64>(col); }
		float GetFloat(int16 col) const { return result_.get<float>(col); }
		std::string GetString(int16 col) const { return result_.get<std::string>(col); }
		bool IsNull(int16 col) const { return result_.is_null(col); }

		// ── 이름 기반 접근 ──

		int32 GetInt32(const std::string& colName) const { return result_.get<int32>(colName); }
		int64 GetInt64(const std::string& colName) const { return result_.get<int64>(colName); }
		float GetFloat(const std::string& colName) const { return result_.get<float>(colName); }
		std::string GetString(const std::string& colName) const { return result_.get<std::string>(colName); }
		bool IsNull(const std::string& colName) const { return result_.is_null(colName); }

		// ── 메타 ──

		bool IsValid() const { return valid_; }
		int32 AffectedRows() const { return affectedRows_; }

	private:
		nanodbc::result result_;
		bool valid_ = false;
		int32 affectedRows_ = 0;
	};

} // namespace mmo
```

- [ ] **Step 2: Commit**

```bash
git add ServerCore/Database/DbResult.h
git commit -m "feat: add DbResult class for query result access"
```

---

## Chunk 3: DbManager

### Task 5: DbManager 클래스 구현

**Files:**
- Create: `ServerCore/Database/DbManager.h`
- Create: `ServerCore/Database/DbManager.cpp`

- [ ] **Step 1: DbManager.h 작성**

```cpp
#pragma once

#include "Database/DbConnection.h"
#include "Database/DbResult.h"

namespace mmo
{

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

	private:
		std::shared_ptr<DbConnection> Acquire();
		void Release(std::shared_ptr<DbConnection> conn);

		DbResult ExecuteInternal(DbConnection& conn,
			const std::string& query);

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
		if constexpr (std::is_same_v<T, std::string>)
			stmt.bind(index, value.c_str());
		else if constexpr (std::is_same_v<T, const char*>)
			stmt.bind(index, value);
		else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>)
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

} // namespace mmo
```

- [ ] **Step 2: DbManager.cpp 작성**

```cpp
#include "pch.h"
#include "Database/DbManager.h"

namespace mmo
{

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

} // namespace mmo
```

- [ ] **Step 3: Commit**

```bash
git add ServerCore/Database/DbManager.h ServerCore/Database/DbManager.cpp
git commit -m "feat: add DbManager with connection pool and sync/async API"
```

---

## Chunk 4: 빌드 통합

### Task 6: ServerCore.vcxproj에 Database 파일 등록

**Files:**
- Modify: `ServerCore/ServerCore.vcxproj`

- [ ] **Step 1: ClCompile 항목에 Database .cpp 파일 추가**

`ServerCore.vcxproj`의 `<ItemGroup>` (ClCompile) 끝에 추가:

```xml
        <ClCompile Include="Database\DbConnection.cpp"/>
        <ClCompile Include="Database\DbManager.cpp"/>
```

- [ ] **Step 2: ClInclude 항목에 Database .h 파일 추가**

`ServerCore.vcxproj`의 `<ItemGroup>` (ClInclude) 끝에 추가:

```xml
        <ClInclude Include="Database\DbConnection.h"/>
        <ClInclude Include="Database\DbResult.h"/>
        <ClInclude Include="Database\DbManager.h"/>
```

- [ ] **Step 3: 전체 빌드 확인**

```bash
msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m
```

Expected: 0 errors, 솔루션 전체 빌드 성공.

- [ ] **Step 4: Commit**

```bash
git add ServerCore/ServerCore.vcxproj ServerCore/pch.h
git commit -m "chore: register Database files in ServerCore project"
```

---

## Chunk 5: 사용 예시 (참고용)

아래는 각 서버에서 DbManager를 통합하는 예시 코드. 이 Task는 실제 구현이 아닌 참고 가이드.

### GameServer에서 사용

```cpp
// GameServer::Init()
auto dbManager = std::make_shared<DbManager>(
    "DRIVER={ODBC Driver 17 for SQL Server};"
    "SERVER=localhost;DATABASE=mmo_game;UID=sa;PWD=yourpass;",
    /*poolSize=*/4, /*workerThreads=*/2
);
dbManager->Start();
GetServiceLocator().Register<DbManager>(dbManager);
```

### LoginServer에서 사용

```cpp
// LoginServer::Init()
auto dbManager = std::make_shared<DbManager>(
    "DRIVER={ODBC Driver 17 for SQL Server};"
    "SERVER=localhost;DATABASE=mmo_login;UID=sa;PWD=yourpass;",
    /*poolSize=*/2, /*workerThreads=*/1
);
dbManager->Start();
GetServiceLocator().Register<DbManager>(dbManager);
```

### 패킷 핸들러에서 비동기 쿼리

```cpp
void HandleLogin(std::shared_ptr<LoginSession> session, Proto::C_Login& pkt)
{
    auto db = session->GetServiceLocator().Get<DbManager>();

    db->AsyncExecute(session->GetIoContext(),
        "SELECT id, password_hash FROM Accounts WHERE username = ?",
        [session](DbResult result) {
            if (result.Next()) {
                auto id = result.GetInt32("id");
                auto hash = result.GetString("password_hash");
                // 비밀번호 검증 후 응답...
            }
        },
        pkt.username());
}
```

### 동기 쿼리 (서버 초기화 시)

```cpp
// 서버 시작 시 설정 데이터 로드
auto result = dbManager->Execute("SELECT key, value FROM ServerConfig");
while (result.Next()) {
    auto key = result.GetString("key");
    auto value = result.GetString("value");
    LOG_INFO(std::format("Config: {} = {}", key, value));
}
```
