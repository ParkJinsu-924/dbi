# Monster FSM AI Design

## Context

현재 Monster는 `CircularMovement`만 수행하고 있어 플레이어와의 상호작용이 전혀 없다. 기본 전투 AI(감지 → 추적 → 공격 → 복귀)를 FSM으로 구현하여 몬스터가 플레이어를 인식하고 전투할 수 있게 한다.

## Approach: Finite State Machine (FSM)

상태 4개(Idle, Chase, Attack, Return)를 enum으로 정의하고, `Monster::Update()`에서 switch/case로 상태별 로직을 실행한다. 별도의 FSM 프레임워크 없이 Monster 클래스에 직접 통합한다.

BT(Behavior Tree)와 Utility AI도 검토했으나, 4개 상태 규모에서는 FSM이 구현/디버깅 비용 대비 가장 효율적이다. 향후 상태가 10개 이상으로 늘어나면 HFSM으로 확장 가능하다.

## States & Transitions

```
[Idle] --detectRange 내 플레이어--> [Chase]
[Chase] --attackRange 도달--> [Attack]
[Chase] --leashRange 초과 OR 타겟 소멸--> [Return]
[Attack] --타겟이 attackRange 벗어남--> [Chase]
[Attack] --타겟 사망/로그아웃 OR leashRange 초과--> [Return]
[Return] --스폰지점 도착(거리 ≤ 1.0f)--> [Idle]
```

- Return 중에는 플레이어를 무시한다 (복귀 완료 후 Idle에서만 재감지)

## Parameters

| 파라미터 | 기본값 | 설명 |
|----------|--------|------|
| `detectRange` | 10.0f | 플레이어 감지 거리 |
| `attackRange` | 2.0f | 근접 공격 가능 거리 |
| `leashRange` | 15.0f | 최대 추적 거리 (스폰지점 기준) |
| `moveSpeed` | 3.0f | 이동 속도 (units/sec) |
| `attackCooldown` | 1.5f | 공격 간격 (초) |
| `attackDamage` | 10 | 공격당 데미지 |

## Code Architecture

### Monster.h 변경

```cpp
enum class MonsterState : uint8_t
{
    Idle,
    Chase,
    Attack,
    Return
};
```

Monster 클래스에 추가할 멤버:

```
// FSM 상태
MonsterState state_ = MonsterState::Idle;
Proto::Vector3 spawnPos_;          // 스폰 지점 (Return 목표)
long long targetGuid_ = 0;        // 추적 중인 플레이어 GUID
float attackTimer_ = 0.0f;        // 공격 쿨다운 타이머

// AI 파라미터
float detectRange_ = 10.0f;
float attackRange_ = 2.0f;
float leashRange_ = 15.0f;
float moveSpeed_ = 3.0f;
float attackCooldown_ = 1.5f;
int32 attackDamage_ = 10;
```

추가할 메서드:

```
void UpdateIdle(float deltaTime);
void UpdateChase(float deltaTime);
void UpdateAttack(float deltaTime);
void UpdateReturn(float deltaTime);
void ChangeState(MonsterState newState);

// 유틸리티
std::shared_ptr<Player> FindNearestPlayer();
float DistanceTo(const Proto::Vector3& target) const;
float DistanceToSpawn() const;
void MoveToward(const Proto::Vector3& target, float deltaTime);
```

### Monster::Update() 변경

기존 CircularMovement 로직을 FSM으로 교체:

```cpp
void Monster::Update(float deltaTime)
{
    switch (state_)
    {
    case MonsterState::Idle:    UpdateIdle(deltaTime);    break;
    case MonsterState::Chase:   UpdateChase(deltaTime);   break;
    case MonsterState::Attack:  UpdateAttack(deltaTime);  break;
    case MonsterState::Return:  UpdateReturn(deltaTime);  break;
    }
}
```

### 상태별 로직 요약

**UpdateIdle**: FindNearestPlayer() 호출 → detectRange 내 플레이어 발견 시 targetGuid_ 설정 후 Chase 전환

**UpdateChase**: 타겟 방향으로 MoveToward() → attackRange 이내면 Attack 전환, leashRange 초과 or 타겟 소멸이면 Return 전환

**UpdateAttack**: attackTimer_ 감소 → 0 이하면 타겟에 TakeDamage() + S_MonsterAttack 브로드캐스트 + 타이머 리셋. 타겟이 attackRange 밖이면 Chase, 소멸/leash 초과면 Return

**UpdateReturn**: spawnPos_ 방향으로 MoveToward() → 도착(≤1.0f)하면 Idle 전환 + HP 회복

### Zone 연동

- Monster가 Zone 참조를 가져야 함 (FindNearestPlayer 구현용)
- `Zone::GetPlayers()` 또는 objects_를 순회하며 Player 타입 필터링
- 기존 `Zone::objects_`는 shared_mutex로 보호되므로 read lock으로 안전하게 접근

### MonsterManager::Spawn() 변경

- `InitCircularMovement()` 대신 `InitAI()` 메서드 추가 (또는 Spawn 파라미터 확장)
- spawnPos_ 설정, AI 파라미터 초기화

## New Packets

### game.proto 추가

```protobuf
// 몬스터 상태 변경 알림
message S_MonsterState {
    int64 guid = 1;
    uint32 state = 2;    // 0=Idle, 1=Chase, 2=Attack, 3=Return
    int64 target_guid = 3; // Chase/Attack 시 타겟 플레이어 GUID
}

// 몬스터 공격 발생
message S_MonsterAttack {
    int64 monster_guid = 1;
    int64 target_guid = 2;
    int32 damage = 3;
}

// 플레이어 HP 변경
message S_PlayerHp {
    int32 hp = 1;
    int32 max_hp = 2;
}
```

패킷 추가 후 `generate_proto.bat` 실행으로 PacketId 자동 생성.

## Files to Modify

| 파일 | 변경 내용 |
|------|-----------|
| `mmosvr/GameServer/Monster.h` | MonsterState enum, AI 멤버/메서드 추가 |
| `mmosvr/GameServer/Monster.cpp` | FSM Update 로직 구현 |
| `mmosvr/GameServer/Zone.h` | GetPlayers() 또는 플레이어 접근 메서드 추가 |
| `mmosvr/GameServer/Zone.cpp` | 플레이어 접근 메서드 구현 |
| `mmosvr/GameServer/MonsterManager.h/cpp` | Spawn() AI 파라미터 확장 |
| `mmosvr/GameServer/main.cpp` | SpawnTestMonsters() AI 파라미터로 변경 |
| `ShareDir/proto/game.proto` | S_MonsterState, S_MonsterAttack, S_PlayerHp 추가 |

## Scope Exclusions

- 플레이어 사망/리스폰 처리 (HP 감소까지만)
- 몬스터 사망 처리
- 플레이어의 몬스터 공격 (클라이언트 → 서버 공격 패킷)
- 경로 탐색 (pathfinding) — 직선 이동만
- 시야/장애물 판정 — 거리 기반 감지만

## Verification

1. **빌드 확인**: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
2. **패킷 생성 확인**: `generate_proto.bat` 실행 후 PacketId 자동 생성 확인
3. **동작 테스트**:
   - GameServer + DummyClient 실행
   - 몬스터가 Idle 상태에서 대기하는지 확인
   - 플레이어가 detectRange에 진입하면 Chase 시작 확인
   - attackRange 도달 시 Attack 전환 + 데미지 적용 확인
   - 플레이어가 leashRange 밖으로 이동 시 Return 확인
   - Return 후 Idle 복귀 확인
4. **로그 확인**: 상태 전환 시 로그 출력으로 FSM 동작 검증
