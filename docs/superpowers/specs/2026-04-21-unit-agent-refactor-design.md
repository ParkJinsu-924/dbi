# Unit Agent Refactor — Design Spec

**Date**: 2026-04-21
**Scope**: `mmosvr/GameServer`

## 1. 문제 정의

현재 `Unit`/`Player`/`Monster` 는 기능별 컨텐츠의 소유 위치가 분산되어 있다.

| 기능 | 현재 소유자 | 자료구조 |
|---|---|---|
| Buff / CC | `Unit::buffs_` (멤버 객체) | `BuffContainer` |
| Buff facade | `Unit` (약 10개 wrapper) | `CanAct/CanMove/…`, `ApplyEffect/TickBuffs/…` |
| Skill cooldown (Player) | `Player::skillCooldowns_` | `unordered_map<int32, float>` |
| Skill cooldown (Monster) | `Monster::skillNextUsable_` | `unordered_map<int32, float>` |
| FSM | `Monster::fsm_` | `MonsterFSM` |
| Aggro | `Monster::aggro_` | `AggroTable` |

문제점:
- 같은 개념(skill cooldown)이 Player 와 Monster 에 중복 구현.
- 새 기능(스탯, 인벤토리 등)을 추가할 때마다 `Unit`/`Player`/`Monster` 중 어디에 붙일지 정해야 하고, 그때마다 Unit facade 가 늘어나는 악순환.
- `Unit` 클래스가 Buff 위임 코드로 비대해진다.

## 2. 목표

각 기능을 **Agent** 라는 단위로 분리하고, `Unit` 은 Agent 컨테이너 역할만 한다. 접근은 `unit->Get<T>()` 하나로 통일한다.

## 3. 설계 원칙

1. **정적 구성**: Agent 등록은 `Unit` 서브클래스의 ctor 에서 1회. 런타임 중 add / remove 없음.
2. **Get 은 실패하지 않는다**: `Get<T>()` 는 `T&` 를 반환하고, 없는 Agent 접근은 프로그래머 버그로 간주해 `assert` 로 즉시 종료한다. 호출자는 항상 해당 Agent 의 존재를 보장해야 한다.
3. **등록 순서 = Tick 순서**: 별도 priority 개념 없음. Base → Derived ctor 순으로 자연스럽게 의존성이 해결된다 (Buff 가 FSM 보다 먼저 tick).
4. **Unit facade 전면 제거**: `Unit` 은 Agent 컨테이너 + 순수 상태(HP, position 등) 외의 기능 API 를 두지 않는다. CC 질의조차 `unit->Get<BuffAgent>().CanAct()` 로 통일.

## 4. 인터페이스

### 4.1 IAgent 베이스

```cpp
class IAgent
{
public:
    virtual ~IAgent() = default;
    virtual void Tick(float /*dt*/) {}   // 기본 no-op — Agent 가 자율 선택

protected:
    explicit IAgent(Unit& owner) : owner_(owner) {}
    Unit& owner_;
};
```

### 4.2 Unit 컨테이너 API

```cpp
class Unit : public GameObject
{
public:
    template<typename T, typename... Args>
    T& AddAgent(Args&&... args)
    {
        static_assert(std::is_base_of_v<IAgent, T>, "T must inherit IAgent");
        auto p = std::make_unique<T>(*this, std::forward<Args>(args)...);
        T& ref = *p;
        tickOrder_.push_back(p.get());
        agents_[std::type_index(typeid(T))] = std::move(p);
        return ref;
    }

    template<typename T>
    T& Get()
    {
        auto it = agents_.find(std::type_index(typeid(T)));
        assert(it != agents_.end() && "Agent not registered on this Unit");
        return *static_cast<T*>(it->second.get());
    }

    void Update(float dt) override
    {
        for (auto* a : tickOrder_) a->Tick(dt);
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<IAgent>> agents_;
    std::vector<IAgent*> tickOrder_;
};
```

**왜 `unordered_map` + 별도 `tickOrder_` 벡터?** `Get<T>()` 는 O(1) 해시 조회가 필요하지만 Tick 은 등록 순서를 지켜야 한다. map 단일 저장소로는 순서가 보장되지 않으므로 포인터 벡터로 Tick 순서를 따로 유지. Agent 수가 작고 수명이 Unit 과 같아 raw pointer 가 안전.

## 5. Agent 라인업

| Agent | 소속 | 책임 | 대체 대상 |
|---|---|---|---|
| `BuffAgent` | Unit 공통 | Buff/Debuff 엔트리 관리, CC 플래그 집계, 스탯 modifier, `CanAct/CanMove/CanAttack/CanCastSkill/CanIgnoreDamage`, `EffectiveMoveSpeed`, `ApplyEffect/Remove` | `Unit::buffs_` (BuffContainer) + Unit 의 facade 약 10개 |
| `SkillCooldownAgent` | Unit 공통 | `bool TryConsume(int32 skillId, float cooldownSec)`, `bool IsReady(sid, now) const`, `void MarkUsed(sid, nextUsable)` | `Player::skillCooldowns_`, `Monster::skillNextUsable_` (두 중복을 통합) |
| `FSMAgent` | Monster 전용 | `MonsterFSM` 소유, `ChangeState/GetCurrentStateId/AddState`, Tick 에서 `owner_.Get<BuffAgent>().CanAct()` 체크 후 `fsm_.Update` | `Monster::fsm_` |
| `AggroAgent` | Monster 전용 | `AggroTable` 소유, `Add/GetTop/Clear/HasAggro` | `Monster::aggro_` |

### 5.1 Monster 전용 Agent 에서 owner 의 서브타입 접근

`FSMAgent`, `AggroAgent` 는 `Monster&` 가 필요한 경우가 있다 (`MonsterFSM::Start(Monster&, …)` 등). 해결 방식:

- `IAgent` 의 `owner_` 는 `Unit&` 로 유지.
- Monster 전용 Agent 의 ctor 에서 `static_cast<Monster&>(owner)` 로 down-cast. 이 Agent 가 Monster 에만 붙는다는 계약(등록 지점이 `Monster::Monster()` 뿐)이 downcast 의 안전성을 보장한다.

```cpp
class FSMAgent : public IAgent
{
public:
    explicit FSMAgent(Unit& owner)
        : IAgent(owner), monsterOwner_(static_cast<Monster&>(owner))
    {
        assert(owner.GetType() == GameObjectType::Monster);
    }

    void Tick(float dt) override
    {
        if (!owner_.Get<BuffAgent>().CanAct()) return;
        fsm_.Update(dt);
    }

    // delegate API
    MonsterFSM& GetFSM() { return fsm_; }
    MonsterStateId GetCurrentStateId() const { return fsm_.GetCurrentStateId(); }
    void ChangeState(MonsterStateId s) { fsm_.ChangeState(s); }

private:
    Monster& monsterOwner_;
    MonsterFSM fsm_;
};
```

## 6. 등록 규칙

```cpp
// Unit ctor 에서 공통 Agent
Unit::Unit(...) : GameObject(...) {
    AddAgent<BuffAgent>();
    AddAgent<SkillCooldownAgent>();
}

// Monster ctor 에서 Monster 전용 Agent 추가
Monster::Monster(...) : Npc(...) {
    AddAgent<FSMAgent>();
    AddAgent<AggroAgent>();
}
```

**결과 Tick 순서**: `Buff → SkillCooldown → FSM → Aggro`.

현재 Monster 의 핵심 의존성(`TickBuffs` → `CanAct` 체크 → `fsm_.Update`)은 FSMAgent::Tick 내부의 `if (!owner_.Get<BuffAgent>().CanAct()) return;` 로 유지된다.

## 7. 호출 측 변화 (Before / After)

```cpp
// Buff
if (!unit->CanMove()) return;                   // before
if (!unit->Get<BuffAgent>().CanMove()) return;  // after

unit->ApplyEffect(eff, *caster);
unit->Get<BuffAgent>().ApplyEffect(eff, *caster);

// Skill cooldown
if (!player->TryConsumeCooldown(sid, cd)) return;
if (!player->Get<SkillCooldownAgent>().TryConsume(sid, cd)) return;

// FSM
owner.GetFSM().ChangeState(MonsterStateId::Engage);
owner.Get<FSMAgent>().ChangeState(MonsterStateId::Engage);

// Aggro
monster->AddAggro(guid, amount);
monster->Get<AggroAgent>().Add(guid, amount);
```

## 8. Phase 계획

각 Phase 는 별도 커밋 / PR. 각 단계 종료 시 빌드 / 런타임 계속 정상.

| Phase | 내용 | 파급 범위 |
|---|---|---|
| 1 | `IAgent` 선언, `Unit::AddAgent/Get<T>/Update` 인프라 도입. 기존 `buffs_`/`fsm_`/cooldown 은 그대로 유지. | 추가만 — 기존 영향 0 |
| 2 | `BuffAgent` 도입. `Unit::buffs_` 제거, Unit facade 10개 제거. Buff 호출처 전부 `Get<BuffAgent>()` 형태로 수정. | Unit.h, BuffContainer → BuffAgent, 전체 `Can*` / `ApplyEffect` 호출처 |
| 3 | `SkillCooldownAgent` 도입. `Player::skillCooldowns_` / `Monster::skillNextUsable_` 제거, 통합 API 로 교체. | Player.h/.cpp, Monster.h/.cpp, GamePacketHandler.cpp |
| 4 | `FSMAgent` 도입. `Monster::fsm_` 제거, `GetFSM()/GetStateId()` 등 호출처 수정. | Monster.h/.cpp, MonsterStates.cpp |
| 5 | `AggroAgent` 도입. `Monster::aggro_` 제거, Aggro delegate 수정. | Monster.h/.cpp |

## 9. 테스트 전략

- 각 Phase 종료 후: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m` 통과.
- DummyClient 로 기본 흐름 (로그인, 이동, 스킬, 몬스터 어그로/공격/스턴) 확인.
- Phase 2 (가장 파급 큼) 후에는 debug_tool 로 버프 stun/silence/root 가 의도대로 동작하는지 확인.
- Phase 3 후에는 Player 스킬 쿨다운 / Monster 스킬 로테이션 동작 확인.

## 10. 비목표 (Non-Goals)

- Agent 의 런타임 동적 add / remove. ctor 등록 후 고정.
- ECS 전환. Unit 은 여전히 OOP 상속 계층.
- Projectile 등 Unit 이 아닌 GameObject 의 변경. (Projectile 은 Agent 체제 밖)
- 기존 Zone / PacketHandler / Manager 구조 변경 — 이 리팩토링의 범위는 Unit 계층 내부에 한정.
