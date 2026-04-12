# DB Manager Design Spec

## Overview

ServerCore에 ODBC 기반 DB 접근 계층을 추가한다. nanodbc 라이브러리를 사용하여 MSSQL/MySQL 모두 지원하며, 커넥션 풀과 동기/비동기 이중 API를 제공한다.

## Goals

- 모든 서버(GameServer, LoginServer 등)에서 공통으로 사용 가능
- 동기/비동기 쿼리 API 모두 제공
- 비동기 콜백은 지정된 io_context(I/O 스레드)에서 실행
- N개 커넥션 풀로 동시 쿼리 처리
- MSSQL/MySQL 드라이버 교체만으로 DB 전환 가능

## Non-Goals

- ORM / 테이블 매핑
- 트랜잭션 관리 (향후 확장 가능)
- 마이그레이션 도구

## File Structure

```
ServerCore/Database/
├── DbManager.h/.cpp      ← 커넥션 풀 + 동기/비동기 API + DB 전용 스레드풀
├── DbConnection.h/.cpp   ← nanodbc::connection 래퍼 (연결 상태, 재연결)
└── DbResult.h            ← nanodbc::result 래퍼 (행/컬럼 접근)
```

## Dependencies

- `nanodbc:x64-windows` (vcpkg)
- 기존: Boost.Asio, C++20

## Component Design

### DbConnection

nanodbc::connection 래퍼. 연결 상태 추적 및 재연결 기능.

```cpp
class DbConnection {
    nanodbc::connection conn_;
    std::string connectionString_;
    bool inUse_ = false;

public:
    DbConnection(const std::string& connStr);
    void Connect();
    void Reconnect();
    bool IsConnected() const;
    bool IsInUse() const;
    void SetInUse(bool use);
    nanodbc::connection& Raw();
};
```

### DbResult

쿼리 결과 래퍼. 인덱스/이름 기반 컬럼 접근.

```cpp
class DbResult {
    nanodbc::result result_;
    bool valid_ = false;
    int32 affectedRows_ = 0;

public:
    DbResult() = default;
    DbResult(nanodbc::result result);
    DbResult(int32 affectedRows);

    bool Next();

    // 인덱스 기반
    int32 GetInt32(int16 col) const;
    int64 GetInt64(int16 col) const;
    float GetFloat(int16 col) const;
    std::string GetString(int16 col) const;
    bool IsNull(int16 col) const;

    // 이름 기반
    int32 GetInt32(const std::string& colName) const;
    int64 GetInt64(const std::string& colName) const;
    float GetFloat(const std::string& colName) const;
    std::string GetString(const std::string& colName) const;
    bool IsNull(const std::string& colName) const;

    bool IsValid() const;
    int32 AffectedRows() const;
};
```

### DbManager

핵심 클래스. 커넥션 풀 + DB 전용 스레드풀 + 동기/비동기 API.

```cpp
class DbManager {
    std::vector<std::shared_ptr<DbConnection>> pool_;
    std::mutex poolMutex_;
    std::condition_variable poolCv_;

    net::io_context dbIoc_;
    net::executor_work_guard<net::io_context::executor_type> workGuard_;
    std::vector<std::jthread> workers_;

    std::string connectionString_;

public:
    DbManager(const std::string& connStr, int32 poolSize, int32 workerThreads);
    ~DbManager();

    void Start();
    void Stop();

    // 동기 API
    template<typename... Args>
    DbResult Execute(const std::string& query, Args&&... args);

    // 비동기 API
    template<typename Callback, typename... Args>
    void AsyncExecute(net::io_context& targetIoc,
                      const std::string& query,
                      Callback&& callback,
                      Args&&... args);

private:
    std::shared_ptr<DbConnection> Acquire();
    void Release(std::shared_ptr<DbConnection> conn);
};
```

## Async Flow

```
호출 스레드 → post(dbIoc_) → DB 워커 스레드에서 쿼리 실행
                              → post(targetIoc) → I/O 스레드에서 콜백 실행
```

## Thread Safety

- Acquire()/Release(): poolMutex_ + condition_variable로 보호
- 각 쿼리는 독립된 커넥션에서 실행 → 쿼리 간 충돌 없음
- 비동기 콜백은 post(targetIoc)로 I/O 스레드에서 실행

## Usage Examples

```cpp
// 초기화 (ServerBase::Init 등)
auto dbManager = std::make_shared<DbManager>(
    "DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost;DATABASE=mmo;UID=sa;PWD=1234;",
    /*poolSize=*/4, /*workerThreads=*/2
);
dbManager->Start();
GetServiceLocator().Register<DbManager>(dbManager);

// 동기 쿼리
auto result = dbManager->Execute("SELECT id, name FROM Players WHERE id = ?", 42);
while (result.Next()) {
    auto id = result.GetInt32("id");
    auto name = result.GetString("name");
}

// 비동기 쿼리
dbManager->AsyncExecute(session->GetIoContext(),
    "UPDATE Players SET level = ? WHERE id = ?",
    [session](DbResult result) {
        LOG_INFO("Updated {} rows", result.AffectedRows());
    },
    newLevel, playerId);
```

## Integration

- pch.h에 `<nanodbc/nanodbc.h>` 추가
- ServerCore.vcxproj에 Database/ 소스 파일 추가
- vcpkg: `vcpkg install nanodbc:x64-windows`
