# Monster Engage State Refactor — Design Spec

**Date:** 2026-04-20
**Project:** mmosvr / GameServer
**Target files:** `MonsterStates.{h,cpp}`, `Monster.{h,cpp}`, `SkillTemplate.*`, `MonsterSkillEntry.*`, `MonsterManager.*`, `monster_skills.csv` (schema)

---

## 1. Problem

현재 `MonsterStates.cpp` 의 FSM 은 `Idle / Patrol / Chase / Attack / Return` 5개 상태를 가진다.
`ChaseState::OnUpdate` 에는 "basic 사거리 밖이지만 사거리 더 긴 special 스킬이 쿨다운·사거리 통과 시 그 자리에서 시전" 하는 로직이 박혀 있다 (`MonsterStates.cpp` L150-156).

이는 **"Chase 상태 안에서 공격이 일어난다"** 는 개념적 모순이다. 반대로 그 로직을 `AttackState` 로 옮기면 **"Attack 상태 안에서 추격이 일어난다"** 는 동일한 모순이 발생한다.

근본 원인은 FSM 의 **"한 순간에 단 하나의 상태"** 모델이, 이동·시전이 연속적으로 섞이는 전투 행동을 잘 표현하지 못한다는 점이다. "Chase / Attack" 구분은 로직 단위가 아니라 사실상 **애니메이션/디버그 표현용 태그**에 가깝다.

또한 현재 구조는 새로운 특수 스킬(예: 돌진, 광역, 조건부 발동 등)을 추가할 때마다 `ChaseState`/`AttackState` 내부를 수정해야 하는 확장성 문제를 가진다.

---

## 2. Goals

1. "Chase 중 공격" / "Attack 중 이동" 같은 개념적 모순을 제거한다.
2. 새로운 특수 스킬을 **기존 AI 코드를 건드리지 않고** 추가할 수 있는 확장점(Strategy) 을 연다.
3. 현재 관측되는 AI 행동 (aggro, leash, 기본 접근, 쿨다운 기반 스킬 선택) 을 **전부 그대로 보존**한다.
4. debug_tool 에서 몬스터가 "접근 중 / 시전 중 / 대기 중" 인지 여전히 구분 가능해야 한다.

## 3. Non-Goals (YAGNI)

다음 항목은 이번 리팩토링 범위에서 **제외** 한다. 필요해지는 시점에 추가한다.

- 스킬 캐스트 타임 (선딜) 및 시전 중 이동 억제 타이머
- 광역(AoE) 판정, 조건부 발동(HP threshold) 같은 구체 스킬 유형
- HSM (부모-자식 계층 상태 머신)
- Behavior Tree / Utility AI 같은 AI 아키텍처 전면 교체
- 스킬 간 선후 의존(combo) 시스템

단, 이후 이 항목들이 추가될 때 **AI 상태 머신을 수정하지 않고 스킬 쪽에서만 구현 가능**해야 한다.

---

## 4. Design

### 4.1 FSM 상태 세트 (변경)

```
Before: Idle / Patrol / Chase / Attack / Return
After:  Idle / Patrol / Engage / Return
```

- `Chase`, `Attack` 제거 → `Engage` 하나로 통합
- `Engage` = "타겟 획득 후 전투 행동 전체" 를 담당하는 단일 상태

### 4.2 `EngageState` 내부 동작

```cpp
class EngageState : public IState<Monster>
{
public:
    void OnEnter(Monster& owner) override;
    void OnUpdate(Monster& owner, float deltaTime) override;

    enum class Phase : uint8_t { Approach, Casting, Waiting };
    Phase GetPhase() const { return phase_; }

private:
    Phase phase_ = Phase::Approach;
};
```

`OnUpdate` 의사코드:

```
1. 매 틱 top aggro 재계산, 현재 target 과 다르면 SetTarget.
2. target 이 없거나 죽었거나 leash 초과면 → Return 전이.
3. dist = DistanceTo(target)
4. choice = owner.PickCastable(now, dist)
   - 쿨다운 통과 + 사거리 통과 + behavior.CanCast(...) 통과 후보 중 가중 추첨
5. if (choice) {
       phase_ = Casting
       choice->behavior->Execute(owner, *target, now)
       owner.MarkSkillUsed(choice->skillId, now + choice->appliedCooldown)
       return   // 캐스트 틱에는 이동하지 않는다
   }
6. if (dist > owner.GetBasicSkillRange()) {
       phase_ = Approach
       owner.MoveToward(target->GetPosition(), deltaTime)
   } else {
       phase_ = Waiting   // 사거리 안인데 시전 가능 스킬 없음, 제자리 대기
   }
```

### 4.3 `phase_` 의 성격

- 로직 분기용이 **아니다**. 매 틱 `PickCastable` 과 거리 비교로 분기가 자연스럽게 결정된다.
- `phase_` 는 이 틱에 **무엇이 일어났는지 기록하는 관측 태그**다.
- debug_tool 에 `Engage/Approach`, `Engage/Casting`, `Engage/Waiting` 로 노출.
- 향후 HSM 으로 승격하고 싶을 때 이 태그를 그대로 서브상태 ID 로 쓸 수 있다.

### 4.4 스킬 행동 추상화 (Strategy)

`SkillTemplate` 에 `ISkillBehavior` 포인터를 추가한다.

```cpp
class ISkillBehavior
{
public:
    virtual ~ISkillBehavior() = default;

    // 쿨다운/사거리 외 추가 시전 조건. 기본은 항상 true.
    virtual bool CanCast(const Monster& owner, const Player& target, float now) const
    {
        return true;
    }

    // 시전 실행. 데미지 적용, 패킷 브로드캐스트 등.
    virtual void Execute(Monster& owner, Player& target, float now) = 0;

    // 0 = 즉발. 향후 선딜 스킬이 필요해지면 이 값이 사용된다.
    virtual float GetCastTime() const { return 0.0f; }
};

struct SkillTemplate
{
    // ... 기존 필드 (cooldown, range, damage, is_basic 등) ...

    std::shared_ptr<ISkillBehavior> behavior;   // nullptr 불가. 로딩 시 반드시 채워짐.
};
```

`Monster::DoAttack` 은 제거되고, 해당 로직은 `DefaultAttackBehavior::Execute` 로 이동한다.

```cpp
class DefaultAttackBehavior : public ISkillBehavior
{
public:
    void Execute(Monster& owner, Player& target, float now) override
    {
        // 기존 Monster::DoAttack 의 내용을 그대로 옮김:
        //  - 데미지 계산, target.TakeDamage
        //  - aggro 적립
        //  - 스킬 사용 패킷 브로드캐스트
    }
};
```

### 4.5 스킬 → 행동 바인딩

`monster_skills.csv` 스키마에 문자열 컬럼 하나 추가:

| 기존 컬럼 | 신규 |
|-----------|------|
| tid, skill_id, is_basic, cooldown, cast_range, damage, weight, ... | **`behavior`** (문자열, 비어 있으면 `"default"`) |

서버 기동 시 스킬 로딩 루틴에서:

1. `behavior` 문자열 읽기
2. `SkillBehaviorRegistry::Create(name)` 호출 → `shared_ptr<ISkillBehavior>` 반환
3. `SkillTemplate.behavior` 에 대입

`SkillBehaviorRegistry` 는 한 곳에서 이름 → factory 매핑을 보유한다.

```cpp
class SkillBehaviorRegistry
{
public:
    using Factory = std::function<std::shared_ptr<ISkillBehavior>()>;

    static void Register(std::string name, Factory f);
    static std::shared_ptr<ISkillBehavior> Create(const std::string& name);
};

// 부팅 시 1회 등록 (어디에 둘지는 구현 플랜에서 결정):
SkillBehaviorRegistry::Register("default",
    []{ return std::make_shared<DefaultAttackBehavior>(); });
```

**새 특수 스킬을 추가하는 절차:**

1. `ISkillBehavior` 상속 클래스 작성 (예: `FireballBehavior`)
2. `SkillBehaviorRegistry::Register("fireball", ...)` 한 줄 추가
3. `monster_skills.csv` 에 해당 스킬 행의 `behavior` 컬럼을 `"fireball"` 로 지정

**AI 코드 (`EngageState`, `Monster` 등) 는 건드리지 않는다.**

### 4.6 `PickCastable` 변경

현재 쿨다운·사거리만 체크. Behavior 의 `CanCast` 도 추가 체크한다.

```cpp
std::optional<SkillChoice> Monster::PickCastable(float now, float distance) const
{
    // 후보 수집: cooldown 통과 + cast_range 통과 + behavior->CanCast(...) 통과
    // 가중치 기반 추첨은 기존 그대로.
}
```

### 4.7 `MonsterGlobalState` 변경

현재 `case MonsterStateId::Chase / Attack / Return` 에서 아무것도 안 하는 스킵 구조. `Chase`, `Attack` 케이스 삭제, `Engage` 케이스 추가:

```cpp
case MonsterStateId::Engage:
case MonsterStateId::Return:
default:
    break;   // 이미 타겟 잡혀 있음. 감지 로직 불필요.
```

---

## 5. Data Flow

```
[Spawn]
   ↓ OnEnter(Idle)
[Idle] ───idleTime ≥ 4s───→ [Patrol] ───도착───→ [Idle]
   ↑                                                ↓
   │                                       GlobalState: aggro 발견
   │                                                ↓
[Return] ←── leash 초과 or target 사망 ── [Engage]
                                              │
                                     매 틱 PickCastable
                                        ├─ 가능 → Execute() (phase=Casting)
                                        ├─ 아니고 거리 > basicRange → MoveToward (phase=Approach)
                                        └─ 아니고 거리 ≤ basicRange → 대기 (phase=Waiting)
```

---

## 6. Deleted / Moved

| 기존 | 처리 |
|------|------|
| `ChaseState` 클래스 | 삭제 |
| `AttackState` 클래스 | 삭제 |
| `MonsterStateId::Chase` | 삭제 |
| `MonsterStateId::Attack` | 삭제 |
| `Monster::DoAttack(sk, target)` | 삭제 — 내용은 `DefaultAttackBehavior::Execute` 로 이동 |
| `MonsterGlobalState` 의 `Chase/Attack` 케이스 | `Engage` 케이스로 치환 |

---

## 7. Testing / Verification

관찰 가능한 행동이 리팩토링 전/후 동일한지 확인한다.

1. **Aggro 감지 → 접근**: Idle 몬스터 근처에 Player 진입 시, 이전처럼 접근 시작.
2. **기본 스킬 사거리 내 지속 공격**: basic 사거리 내에서 쿨다운 맞춰 반복 시전.
3. **특수 스킬 원거리 시전** (기존 ChaseState 에 있던 분기): basic 사거리 밖이지만 special 스킬의 cast_range 이내일 때, 이동 없이 그 자리에서 시전.
4. **Leash 이탈 시 귀환**: spawn 에서 `leashRange` 초과 이동 시 Return 진입 + HP 회복 + aggro 초기화.
5. **타겟 사망 시 귀환**: 교전 중 target 이 죽으면 Return.
6. **debug_tool 표시**: Engage 중 `Approach/Casting/Waiting` 세 phase 모두 관측 가능.

DummyClient 로 한 마리를 유인·이탈·격파 시나리오를 돌려 육안 확인한다.

---

## 8. Open Questions (구현 단계에서 결정)

- `ISkillBehavior` 헤더 위치: `GameServer/Skill/` 신규 디렉토리 vs 기존 `SkillTemplate.*` 옆
- `SkillBehaviorRegistry` 등록 시점: `GameServer::Init()` 에서 명시적 호출 vs 각 Behavior cpp 의 static initializer (전자 선호 — 순서 제어 가능)
- `monster_skills.csv` 의 기존 행들에 `behavior` 컬럼을 어떻게 채울지 — 전부 빈 값(`default`) 로 둘지 마이그레이션 스크립트 작성할지
