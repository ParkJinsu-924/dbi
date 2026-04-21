# Unit Agent Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `Unit` 이하 기능(Buff / Skill cooldown / FSM / Aggro)을 `IAgent` 기반 컴포지션으로 재구성하고, 접근은 `unit->Get<T>()` 단일 API 로 통일한다.

**Architecture:** `Unit` 은 `AddAgent<T>()` / `Get<T>()` / `Update()` 3 개 API 만 가진 Agent 컨테이너가 된다. 등록 순서 = Tick 순서. `Get<T>()` 는 실패하지 않는 계약(항상 `T&`, 없으면 `assert`). 5 개 Phase 로 단계적 마이그레이션: 인프라 → Buff → SkillCooldown → FSM → Aggro.

**Tech Stack:** C++20, MSBuild (VS 2022), Boost.Asio, Protobuf. 단위 테스트 프레임워크 없음 — 각 Task 는 `msbuild` 빌드 성공 + 런타임 스모크(DummyClient 기본 흐름) 로 검증.

**Spec:** `docs/superpowers/specs/2026-04-21-unit-agent-refactor-design.md`

---

## 파일 구조 맵

```
mmosvr/GameServer/
  Agent/                              ← 새 디렉터리
    IAgent.h                          ← Task 1
    BuffAgent.h, BuffAgent.cpp        ← Task 2 (BuffContainer 흡수)
    SkillCooldownAgent.h, .cpp        ← Task 3
    FSMAgent.h, FSMAgent.cpp          ← Task 4
    AggroAgent.h, AggroAgent.cpp      ← Task 5
  Unit.h                              ← Task 1~5 에 걸쳐 지속 수정
  BuffContainer.h, BuffContainer.cpp  ← Task 2 에서 삭제
  Player.h, Player.cpp                ← Task 2, 3
  Monster.h, Monster.cpp              ← Task 2, 3, 4, 5
  MonsterStates.cpp                   ← Task 2, 4, 5
  GamePacketHandler.cpp               ← Task 2, 3
  SkillBehavior.cpp                   ← Task 2
  SkillRuntime.h                      ← Task 2
  Projectile.cpp                      ← Task 2, 5
  GameServer.vcxproj, .filters        ← Task 1~5
```

**공통 규칙**
- `#include` 경로: Agent/ 폴더는 vcxproj 의 `AdditionalIncludeDirectories` 에 이미 `$(ProjectDir);` 이 있어 `#include "Agent/IAgent.h"` 형태로 쓴다.
- vcxproj / filters 는 Visual Studio 에서 파일 추가하면 자동 갱신. 수동 편집 시 `<ClInclude Include="Agent\IAgent.h" />` / `<ClCompile Include="Agent\BuffAgent.cpp" />` 패턴을 따른다.
- 빌드 명령 (VS Developer Command Prompt, x64): `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
- 런타임 스모크: GameServer.exe 기동 → DummyClient.exe 로 접속 → 기본 이동, 스킬 1회, 몬스터 조우까지 로그 확인.

---

## Task 1: Agent 인프라 (IAgent / Unit::AddAgent·Get·Update)

**Files:**
- Create: `mmosvr/GameServer/Agent/IAgent.h`
- Modify: `mmosvr/GameServer/Unit.h` (전체)
- Modify: `mmosvr/GameServer/GameServer.vcxproj`, `GameServer.vcxproj.filters`

이 Task 의 목표는 **인프라만 추가**. 기존 `buffs_`, `CanAct()` 등 facade, 호출처는 그대로 둔다. 기존 빌드 영향 0.

- [ ] **Step 1: `Agent/` 폴더 생성 + IAgent.h 작성**

디렉터리 생성 후 파일 작성.

`mmosvr/GameServer/Agent/IAgent.h`:
```cpp
#pragma once

class Unit;

// ===========================================================================
// IAgent — Unit 에 부착되는 기능 모듈의 베이스.
// Unit 은 ctor 에서 AddAgent<T>() 로 등록하고, 이후 Get<T>() 로 접근한다.
// Tick 기본 구현은 no-op; 주기적 갱신이 필요한 Agent 만 override.
// ===========================================================================
class IAgent
{
public:
    virtual ~IAgent() = default;

    // Unit::Update 매 틱마다 호출. 등록 순서대로 실행된다.
    virtual void Tick(float /*deltaTime*/) {}

protected:
    // 소유 Unit 에 대한 참조. Unit 수명이 Agent 수명을 포함한다.
    explicit IAgent(Unit& owner) : owner_(owner) {}

    Unit& owner_;
};
```

- [ ] **Step 2: Unit.h 전면 수정**

`mmosvr/GameServer/Unit.h` 를 다음으로 덮어쓴다.

```cpp
#pragma once

#include "GameObject.h"
#include "BuffContainer.h"
#include "Agent/IAgent.h"

#include <cassert>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

struct Effect;


// ===========================================================================
// Unit — Agent 컨테이너 + 기본 생존 상태(HP) 소유.
// 기능별 로직은 Agent (Buff / SkillCooldown / FSM / Aggro 등) 에 위임한다.
// Get<T>() 는 실패하지 않는다: 호출자가 해당 Agent 등록을 보장해야 한다.
// ===========================================================================
class Unit : public GameObject
{
public:
    Unit(GameObjectType type, Zone& zone, std::string name = "")
        : GameObject(type, zone, std::move(name)), buffs_(*this)
    {
    }

    Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
        : GameObject(type, zone, guid, std::move(name)), buffs_(*this)
    {
    }

    // --- Agent API ---------------------------------------------------------

    // ctor 에서 1 회 호출. 런타임 add/remove 는 지원하지 않는다.
    // 등록 순서가 Tick 순서이므로, 의존성 있는 Agent 는 그 의존 대상 이후에 등록한다.
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

    // 해당 타입의 Agent 가 반드시 등록되어 있음을 호출자가 보장해야 한다.
    // 없으면 프로그래머 버그 → assert 로 즉시 종료.
    template<typename T>
    T& Get()
    {
        auto it = agents_.find(std::type_index(typeid(T)));
        assert(it != agents_.end() && "Agent not registered on this Unit");
        return *static_cast<T*>(it->second.get());
    }

    template<typename T>
    const T& Get() const
    {
        auto it = agents_.find(std::type_index(typeid(T)));
        assert(it != agents_.end() && "Agent not registered on this Unit");
        return *static_cast<const T*>(it->second.get());
    }

    // Zone 이 매 프레임 호출. Agent 들을 등록 순서대로 Tick.
    void Update(float deltaTime) override
    {
        for (auto* a : tickOrder_)
            a->Tick(deltaTime);
    }

    // --- HP / 생존 --------------------------------------------------------

    int32 GetHp() const { return hp_; }
    void SetHp(int32 hp) { hp_ = hp; }
    int32 GetMaxHp() const { return maxHp_; }
    void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
    bool IsAlive() const { return hp_ > 0; }

    void TakeDamage(int32 amount)
    {
        if (CanIgnoreDamage()) return;
        hp_ = (std::max)(0, hp_ - amount);
    }
    void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

    // --- Buff facade (Task 2 에서 제거 예정) -------------------------------
    // 이 Task 시점엔 기존 호출처 보존을 위해 유지. Task 2 에서 전부 제거된다.
    void ApplyEffect(const Effect& e, const Unit& caster) { buffs_.ApplyEffect(e, caster.GetGuid()); }
    bool DispelBuff(const int32 eid)                      { return buffs_.Remove(eid); }
    void TickBuffs(const float dt)                        { buffs_.Tick(dt); }

    float GetEffectiveMoveSpeed(float baseSpeed) const { return buffs_.EffectiveMoveSpeed(baseSpeed); }

    bool CanAct()       const { return buffs_.CanAct(); }
    bool CanMove()      const { return buffs_.CanMove(); }
    bool CanAttack()    const { return buffs_.CanAttack(); }
    bool CanCastSkill() const { return buffs_.CanCastSkill(); }
    bool CanIgnoreDamage() const { return buffs_.CanIgnoreDamage(); }

protected:
    int32 hp_ = 100;
    int32 maxHp_ = 100;
    BuffContainer buffs_;   // Task 2 에서 제거

private:
    // Agent 저장소. typeid(T) 로 조회.
    std::unordered_map<std::type_index, std::unique_ptr<IAgent>> agents_;
    // Tick 순서 보존용 raw pointer 벡터 (소유권은 agents_ 에).
    std::vector<IAgent*> tickOrder_;
};
```

- [ ] **Step 3: vcxproj 에 IAgent.h 등록**

`GameServer.vcxproj` 의 `<ItemGroup>` 중 `<ClInclude>` 블록에 한 줄 추가:
```xml
<ClInclude Include="Agent\IAgent.h" />
```

`GameServer.vcxproj.filters` 에도 동일한 `<ClInclude>` 블록 + `<Filter>Agent</Filter>` 를 추가 (filter 가 없으면 `<ItemGroup>` 에 `<Filter Include="Agent"><UniqueIdentifier>{새-GUID}</UniqueIdentifier></Filter>` 도 함께 추가).

VS 에서 파일 추가하면 자동 처리되므로, VS 열어서 "기존 항목 추가 → Agent/IAgent.h" 가 편하다.

- [ ] **Step 4: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`

Unit::Update 가 virtual override 이지만 기존에 `GameObject::Update` 가 `virtual void Update(float) {}` 인 것을 확인했으므로 시그니처 일치. Player::Update, Monster::Update 는 override 로 `Unit::Update` 를 **호출하지 않는다** (아직). 즉 이 Task 시점에는 agents_ 가 비어 있어 Unit::Update 는 no-op. 기존 동작 100% 유지.

- [ ] **Step 5: 런타임 스모크**

GameServer.exe 기동 → DummyClient.exe 로 접속 → 이동/스킬/몬스터 조우. 기존 동작과 동일해야 함.

- [ ] **Step 6: 커밋**

```bash
git add mmosvr/GameServer/Agent/IAgent.h mmosvr/GameServer/Unit.h mmosvr/GameServer/GameServer.vcxproj mmosvr/GameServer/GameServer.vcxproj.filters
git commit -m "feat(unit): Agent 인프라 도입 (IAgent / AddAgent / Get<T>)"
```

---

## Task 2: BuffAgent 도입 + Unit facade 제거

**Files:**
- Create: `mmosvr/GameServer/Agent/BuffAgent.h`, `Agent/BuffAgent.cpp`
- Delete: `mmosvr/GameServer/BuffContainer.h`, `BuffContainer.cpp`
- Modify: `mmosvr/GameServer/Unit.h`
- Modify: `mmosvr/GameServer/Player.cpp`, `Monster.cpp`
- Modify: `mmosvr/GameServer/GamePacketHandler.cpp`, `MonsterStates.cpp`
- Modify: `mmosvr/GameServer/SkillBehavior.cpp`, `SkillRuntime.h`
- Modify: `mmosvr/GameServer/GameServer.vcxproj`, `.filters`

- [ ] **Step 1: BuffAgent.h 작성 — BuffContainer.h 내용 이식**

`mmosvr/GameServer/Agent/BuffAgent.h`:
```cpp
#pragma once

#include "Agent/IAgent.h"
#include "AttackTypes.h"
#include "Utils/Types.h"

#include <vector>

struct Effect;


// ===========================================================================
// BuffAgent — Unit 공통. Buff/Debuff 엔트리 관리 + CC 플래그 집계 + 스탯 modifier.
// Tick 마다 duration 감소, 만료 엔트리 제거 + S_BuffRemoved 방송.
// ApplyEffect: 즉발(Damage/Heal) 은 owner 에 바로, 지속(StatMod/CCState) 은 Add.
// ===========================================================================
class BuffAgent : public IAgent
{
public:
    struct Entry
    {
        const Effect* effect;
        long long     casterGuid;
        float         remainingDuration;
    };

    explicit BuffAgent(Unit& owner) : IAgent(owner) {}

    // IAgent ---------------------------------------------------------------
    void Tick(float dt) override;

    // Buff 추가/제거 -------------------------------------------------------

    // true = 새로 부착되었거나 기존 엔트리가 refresh 됨. false = duration<=0.
    bool Add(const Effect& e, long long casterGuid);

    // Dispel. true = 실제 제거됨.
    bool Remove(int32 eid);

    // SkillRuntime 에서 OnCast/OnHit trigger 당 호출.
    // 즉발(Damage/Heal) 은 owner_ 에 바로 반영, 지속(StatMod/CCState) 은 Add.
    void ApplyEffect(const Effect& e, long long casterGuid);

    // 호출자 편의 overload (caster 의 GUID 추출).
    void ApplyEffect(const Effect& e, const Unit& caster);

    // 조회 / 집계 ----------------------------------------------------------

    const std::vector<Entry>& GetEntries() const { return entries_; }

    uint32 GetCCFlags() const;
    void   GetStatModifier(StatType stat, float& outFlat, float& outPercent) const;
    float  EffectiveMoveSpeed(float baseSpeed) const;

    // CC 상태 질의 (원시) ---------------------------------------------------
    bool IsStunned() const      { return GetCCFlags() & static_cast<uint32>(CCFlag::Stun); }
    bool IsSilenced() const     { return GetCCFlags() & static_cast<uint32>(CCFlag::Silence); }
    bool IsRooted() const       { return GetCCFlags() & static_cast<uint32>(CCFlag::Root); }
    bool IsInvulnerable() const { return GetCCFlags() & static_cast<uint32>(CCFlag::Invulnerable); }

    // 의미적 능력 질의 (어떤 CC 가 어떤 액션을 막는가의 단일 소스) ------------
    bool CanAct()           const { return !IsStunned(); }
    bool CanMove()          const { return !(IsStunned() || IsRooted()); }
    bool CanAttack()        const { return !IsStunned(); }
    bool CanCastSkill()     const { return !(IsStunned() || IsSilenced()); }
    bool CanIgnoreDamage()  const { return IsInvulnerable(); }

private:
    std::vector<Entry> entries_;
};
```

- [ ] **Step 2: BuffAgent.cpp 작성 — BuffContainer.cpp 내용 이식**

`mmosvr/GameServer/Agent/BuffAgent.cpp`:
```cpp
#include "pch.h"
#include "Agent/BuffAgent.h"
#include "Effect.h"
#include "Unit.h"
#include "Zone.h"
#include "PacketMaker.h"


bool BuffAgent::Add(const Effect& e, const long long casterGuid)
{
    if (e.duration <= 0.0f)
        return false;

    for (auto& entry : entries_)
    {
        if (entry.effect->eid == e.eid)
        {
            entry.remainingDuration = e.duration;
            entry.casterGuid        = casterGuid;
            owner_.GetZone().Broadcast(PacketMaker::MakeBuffApplied(owner_.GetGuid(), e, casterGuid));
            return true;
        }
    }

    entries_.push_back({ &e, casterGuid, e.duration });
    owner_.GetZone().Broadcast(PacketMaker::MakeBuffApplied(owner_.GetGuid(), e, casterGuid));
    return true;
}

void BuffAgent::Tick(const float dt)
{
    if (entries_.empty())
        return;

    Zone& zone = owner_.GetZone();

    for (auto it = entries_.begin(); it != entries_.end();)
    {
        it->remainingDuration -= dt;
        if (it->remainingDuration <= 0.0f)
        {
            const int32 expiredEid = it->effect->eid;
            it = entries_.erase(it);
            zone.Broadcast(PacketMaker::MakeBuffRemoved(owner_.GetGuid(), expiredEid));
        }
        else
        {
            ++it;
        }
    }
}

bool BuffAgent::Remove(const int32 eid)
{
    for (auto it = entries_.begin(); it != entries_.end(); ++it)
    {
        if (it->effect->eid == eid)
        {
            entries_.erase(it);
            owner_.GetZone().Broadcast(PacketMaker::MakeBuffRemoved(owner_.GetGuid(), eid));
            return true;
        }
    }
    return false;
}

uint32 BuffAgent::GetCCFlags() const
{
    uint32 flags = 0;
    for (const auto& e : entries_)
        flags |= static_cast<uint32>(e.effect->cc_flag);
    return flags;
}

void BuffAgent::GetStatModifier(const StatType stat, float& outFlat, float& outPercent) const
{
    outFlat = 0.0f;
    outPercent = 0.0f;
    for (const auto& e : entries_)
    {
        const auto* eff = e.effect;
        if (eff->type != EffectType::StatMod || eff->stat != stat)
            continue;
        if (eff->is_percent)
            outPercent += eff->magnitude;
        else
            outFlat    += eff->magnitude;
    }
}

void BuffAgent::ApplyEffect(const Effect& e, const long long casterGuid)
{
    switch (e.type)
    {
    case EffectType::Damage:
        owner_.TakeDamage(static_cast<int32>(e.magnitude));
        break;

    case EffectType::Heal:
        owner_.Heal(static_cast<int32>(e.magnitude));
        break;

    case EffectType::StatMod:
    case EffectType::CCState:
        Add(e, casterGuid);
        break;

    default:
        break;
    }
}

void BuffAgent::ApplyEffect(const Effect& e, const Unit& caster)
{
    ApplyEffect(e, caster.GetGuid());
}

float BuffAgent::EffectiveMoveSpeed(const float baseSpeed) const
{
    float flat = 0.0f, pct = 0.0f;
    GetStatModifier(StatType::MoveSpeed, flat, pct);
    return (std::max)(0.0f, baseSpeed * (1.0f + pct) + flat);
}
```

- [ ] **Step 3: vcxproj / filters 에 BuffAgent.h, .cpp 등록**

`GameServer.vcxproj` 에 `<ClInclude Include="Agent\BuffAgent.h" />` 와 `<ClCompile Include="Agent\BuffAgent.cpp" />` 추가. filters 에도 `<Filter>Agent</Filter>` 와 함께 추가.

동시에 **삭제할** 파일:
- `<ClInclude Include="BuffContainer.h" />`
- `<ClCompile Include="BuffContainer.cpp" />`

(VS 열어서 solution explorer 에서 BuffContainer.* 제거 + Agent/BuffAgent.* 추가 하는 편이 안전하다.)

- [ ] **Step 4: Unit.h 에서 facade 전부 제거 + BuffAgent 등록**

`mmosvr/GameServer/Unit.h` 를 다음으로 교체:
```cpp
#pragma once

#include "GameObject.h"
#include "Agent/IAgent.h"
#include "Agent/BuffAgent.h"

#include <cassert>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>


// ===========================================================================
// Unit — Agent 컨테이너 + 기본 생존 상태(HP) 소유.
// 기능별 로직(Buff / SkillCooldown / FSM / Aggro …) 은 Agent 에 위임.
// Get<T>() 는 실패하지 않는 계약: 없는 Agent 접근은 프로그래머 버그 → assert.
// ===========================================================================
class Unit : public GameObject
{
public:
    Unit(GameObjectType type, Zone& zone, std::string name = "")
        : GameObject(type, zone, std::move(name))
    {
        AddAgent<BuffAgent>();
    }

    Unit(GameObjectType type, Zone& zone, long long guid, std::string name)
        : GameObject(type, zone, guid, std::move(name))
    {
        AddAgent<BuffAgent>();
    }

    // --- Agent API --------------------------------------------------------

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

    template<typename T>
    const T& Get() const
    {
        auto it = agents_.find(std::type_index(typeid(T)));
        assert(it != agents_.end() && "Agent not registered on this Unit");
        return *static_cast<const T*>(it->second.get());
    }

    void Update(float deltaTime) override
    {
        for (auto* a : tickOrder_)
            a->Tick(deltaTime);
    }

    // --- HP / 생존 --------------------------------------------------------

    int32 GetHp() const { return hp_; }
    void SetHp(int32 hp) { hp_ = hp; }
    int32 GetMaxHp() const { return maxHp_; }
    void SetMaxHp(int32 maxHp) { maxHp_ = maxHp; }
    bool IsAlive() const { return hp_ > 0; }

    void TakeDamage(int32 amount)
    {
        if (Get<BuffAgent>().CanIgnoreDamage()) return;
        hp_ = (std::max)(0, hp_ - amount);
    }
    void Heal(int32 amount) { hp_ = (std::min)(maxHp_, hp_ + amount); }

protected:
    int32 hp_ = 100;
    int32 maxHp_ = 100;

private:
    std::unordered_map<std::type_index, std::unique_ptr<IAgent>> agents_;
    std::vector<IAgent*> tickOrder_;
};
```

이 시점에서 Unit.h 는 facade 가 전부 제거됐다. 이제 Unit.h 를 include 하는 모든 호출처가 컴파일 실패한다. 다음 Step 에서 일괄 수정.

- [ ] **Step 5: Player.cpp 호출처 수정**

`mmosvr/GameServer/Player.cpp` 에서 3 군데 수정:
```cpp
// --- line 50 부근: Update 전면 재작성 ---
void Player::Update(const float deltaTime)
{
    Unit::Update(deltaTime);   // ← BuffAgent.Tick 등 Agent 체인 실행

    if (!isMoving_ || !Get<BuffAgent>().CanMove())
        return;

    const float dx = destination_.x() - position_.x();
    const float dz = destination_.y() - position_.y();
    const float dist = MathUtil::Length2D(dx, dz);

    if (dist < 0.001f)
    {
        isMoving_ = false;
        return;
    }

    const float step = Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_) * deltaTime;
    if (step >= dist)
    {
        position_.set_x(destination_.x());
        position_.set_y(destination_.y());
        isMoving_ = false;
        return;
    }

    const float nx = dx / dist;
    const float nz = dz / dist;
    position_.set_x(position_.x() + nx * step);
    position_.set_y(position_.y() + nz * step);
}
```

포인트:
- `TickBuffs(deltaTime)` 제거 → `Unit::Update(deltaTime)` 호출 (Agent 체인)
- `CanMove()` → `Get<BuffAgent>().CanMove()`
- `GetEffectiveMoveSpeed(moveSpeed_)` → `Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_)`

Player.cpp 상단에 `#include "Agent/BuffAgent.h"` 추가 (Unit.h 가 이미 include 하므로 transitive 로 들어오지만 명시가 안전).

- [ ] **Step 6: Monster.cpp 호출처 수정**

`mmosvr/GameServer/Monster.cpp` 수정:
```cpp
// --- line 39 부근: Update ---
void Monster::Update(const float deltaTime)
{
    Unit::Update(deltaTime);   // BuffAgent.Tick 실행

    // 총체적 행동 불가(Stun) 면 FSM 스킵. Task 4 에서 FSMAgent::Tick 내부로 이관 예정.
    if (!Get<BuffAgent>().CanAct())
        return;
    fsm_.Update(deltaTime);
}

// --- line 89 부근: MoveToward ---
void Monster::MoveToward(const Proto::Vector2& target, const float deltaTime)
{
    if (!Get<BuffAgent>().CanMove())
        return;

    const float dx = target.x() - position_.x();
    const float dz = target.y() - position_.y();
    const float dist = MathUtil::Length2D(dx, dz);

    if (dist < 0.001f)
        return;

    float step = Get<BuffAgent>().EffectiveMoveSpeed(moveSpeed_) * deltaTime;
    if (step > dist)
        step = dist;

    position_.set_x(position_.x() + (dx / dist) * step);
    position_.set_y(position_.y() + (dz / dist) * step);
}
```

`#include "Agent/BuffAgent.h"` 추가.

- [ ] **Step 7: GamePacketHandler.cpp 호출처 수정**

`mmosvr/GameServer/GamePacketHandler.cpp` 에서 3 군데:
- line 113: `if (!player->CanMove())` → `if (!player->Get<BuffAgent>().CanMove())`
- line 146: `if (!player->CanMove())` → `if (!player->Get<BuffAgent>().CanMove())`
- line 199: `if (!player->CanCastSkill())` → `if (!player->Get<BuffAgent>().CanCastSkill())`

파일 상단에 `#include "Agent/BuffAgent.h"` 추가.

- [ ] **Step 8: SkillBehavior.cpp / SkillRuntime.h 수정**

`mmosvr/GameServer/SkillBehavior.cpp` line 14:
```cpp
// before
if (!owner.CanAttack()) return;
// after
if (!owner.Get<BuffAgent>().CanAttack()) return;
```
상단에 `#include "Agent/BuffAgent.h"` 추가.

`mmosvr/GameServer/SkillRuntime.h` line 75:
```cpp
// before
victim->ApplyEffect(*e, casterRef);
// after
victim->Get<BuffAgent>().ApplyEffect(*e, casterRef);
```
SkillRuntime.h 상단 include 블록에 `#include "Agent/BuffAgent.h"` 추가.

- [ ] **Step 9: Projectile.cpp 수정 (BuffAgent 관련만)**

`mmosvr/GameServer/Projectile.cpp` line 48 은 `SkillRuntime::ApplyEffects` 호출이라 Step 8 로 이미 처리됨. Buff 쪽 다른 변경 없음. 다음 Step 건너뜀.

- [ ] **Step 10: BuffContainer.h / .cpp 파일 삭제**

디스크에서 삭제:
- `mmosvr/GameServer/BuffContainer.h`
- `mmosvr/GameServer/BuffContainer.cpp`

vcxproj / filters 에서 `BuffContainer.h`, `BuffContainer.cpp` 참조 제거 (Step 3 에서 했으면 재확인).

전역 grep 으로 `BuffContainer` 잔재 없는지 확인 (PCH, 다른 include 등):
```bash
grep -r "BuffContainer" mmosvr/GameServer/
```
결과가 비어 있어야 한다.

- [ ] **Step 11: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`

자주 나올 에러:
- "undeclared identifier 'BuffAgent'" → 해당 파일에 `#include "Agent/BuffAgent.h"` 누락. 추가.
- "no member named 'CanMove' in 'Unit'" → facade 호출처 일부를 놓침. grep 으로 `->CanMove()\|->CanAct()\|->CanAttack()\|->CanCastSkill()\|->ApplyEffect(\|->DispelBuff(\|->TickBuffs(\|->GetEffectiveMoveSpeed(` 재검색.

- [ ] **Step 12: 런타임 스모크**

GameServer → DummyClient 접속. 다음 확인:
- 이동: destination 쪽으로 정상 이동
- 스킬 1 회 사용 + 몬스터 피격: HP 감소 + 버프 (stun/slow) 정상 적용/만료
- 몬스터 agro → 공격: 피격 시 Player HP 감소
- 버프 중 이동/공격 차단: stun 걸린 몬스터가 행동 안 하는지

- [ ] **Step 13: 커밋**

```bash
git add mmosvr/GameServer/Agent/BuffAgent.h mmosvr/GameServer/Agent/BuffAgent.cpp \
        mmosvr/GameServer/Unit.h mmosvr/GameServer/Player.cpp mmosvr/GameServer/Monster.cpp \
        mmosvr/GameServer/GamePacketHandler.cpp mmosvr/GameServer/SkillBehavior.cpp \
        mmosvr/GameServer/SkillRuntime.h mmosvr/GameServer/GameServer.vcxproj \
        mmosvr/GameServer/GameServer.vcxproj.filters
git rm mmosvr/GameServer/BuffContainer.h mmosvr/GameServer/BuffContainer.cpp
git commit -m "refactor(unit): BuffAgent 도입 + Unit facade 제거"
```

---

## Task 3: SkillCooldownAgent 도입 (Player / Monster cooldown 통합)

**Files:**
- Create: `mmosvr/GameServer/Agent/SkillCooldownAgent.h`, `SkillCooldownAgent.cpp`
- Modify: `mmosvr/GameServer/Unit.h`
- Modify: `mmosvr/GameServer/Player.h`, `Player.cpp`
- Modify: `mmosvr/GameServer/Monster.h`, `Monster.cpp`
- Modify: `mmosvr/GameServer/GamePacketHandler.cpp`
- Modify: `mmosvr/GameServer/MonsterStates.cpp`
- Modify: `mmosvr/GameServer/GameServer.vcxproj`, `.filters`

- [ ] **Step 1: SkillCooldownAgent.h 작성**

`mmosvr/GameServer/Agent/SkillCooldownAgent.h`:
```cpp
#pragma once

#include "Agent/IAgent.h"
#include "Utils/Types.h"

#include <unordered_map>


// ===========================================================================
// SkillCooldownAgent — Unit 공통. Skill id 별 "다음 사용 가능 시각" 관리.
// Player 는 TryConsume (소비형), Monster 는 IsReady + MarkUsed (조회 + 설정 분리)
// 두 패턴을 모두 지원한다.
// 시간 기준은 TimeManager.GetTotalTime() (초).
// ===========================================================================
class SkillCooldownAgent : public IAgent
{
public:
    explicit SkillCooldownAgent(Unit& owner) : IAgent(owner) {}

    // 사용 가능하면 다음 사용 가능 시각을 now+cooldownSec 로 갱신하고 true.
    // 쿨다운 중이면 false. (Player 스타일)
    bool TryConsume(int32 skillId, float cooldownSec);

    // now 시점에 사용 가능한지만 조회. (Monster PickCastable 스타일)
    bool IsReady(int32 skillId, float now) const;

    // 스킬 사용을 외부에서 마킹. nextUsable = 다음 사용 가능 시각.
    void MarkUsed(int32 skillId, float nextUsable) { nextUsable_[skillId] = nextUsable; }

private:
    std::unordered_map<int32, float> nextUsable_;
};
```

- [ ] **Step 2: SkillCooldownAgent.cpp 작성**

`mmosvr/GameServer/Agent/SkillCooldownAgent.cpp`:
```cpp
#include "pch.h"
#include "Agent/SkillCooldownAgent.h"


bool SkillCooldownAgent::TryConsume(const int32 skillId, const float cooldownSec)
{
    const float now = GetTimeManager().GetTotalTime();
    auto it = nextUsable_.find(skillId);
    if (it != nextUsable_.end() && now < it->second)
        return false;

    nextUsable_[skillId] = now + cooldownSec;
    return true;
}

bool SkillCooldownAgent::IsReady(const int32 skillId, const float now) const
{
    auto it = nextUsable_.find(skillId);
    return (it == nextUsable_.end()) || (now >= it->second);
}
```

- [ ] **Step 3: vcxproj / filters 에 등록**

Task 2 Step 3 와 동일 패턴으로 Agent/SkillCooldownAgent.h, .cpp 추가.

- [ ] **Step 4: Unit.h 에 SkillCooldownAgent 등록 추가**

`mmosvr/GameServer/Unit.h` 의 두 ctor 수정:
```cpp
// before
Unit(GameObjectType type, Zone& zone, std::string name = "")
    : GameObject(type, zone, std::move(name))
{
    AddAgent<BuffAgent>();
}

// after
Unit(GameObjectType type, Zone& zone, std::string name = "")
    : GameObject(type, zone, std::move(name))
{
    AddAgent<BuffAgent>();
    AddAgent<SkillCooldownAgent>();
}
```

두 번째 ctor 도 동일하게 `AddAgent<SkillCooldownAgent>();` 추가. 상단에 `#include "Agent/SkillCooldownAgent.h"` 추가.

- [ ] **Step 5: Player.h / Player.cpp 에서 cooldown 코드 제거**

`mmosvr/GameServer/Player.h`:
- `bool TryConsumeCooldown(int32 skillId, float cooldownSec);` 선언 제거 (line 54 부근)
- `std::unordered_map<int32, float> skillCooldowns_;` 멤버 제거 (line 72 부근)
- `#include <unordered_map>` 불필요해지면 제거 (다른 곳에서 안 쓰면)

`mmosvr/GameServer/Player.cpp`:
- `Player::TryConsumeCooldown` 구현 제거 (line 37~46)

- [ ] **Step 6: Monster.h / Monster.cpp 에서 cooldown 코드 제거 + 호출처 Agent 전환**

`mmosvr/GameServer/Monster.h`:
- `void MarkSkillUsed(int32 skillId, float nextUsable) { skillNextUsable_[skillId] = nextUsable; }` 제거 (line 81)
- `std::unordered_map<int32, float> skillNextUsable_;` 제거 (line 99)

`mmosvr/GameServer/Monster.cpp` PickCastable 내부 (line 147~148):
```cpp
// before
const auto it = skillNextUsable_.find(entry->skillId);
const float ready = (it == skillNextUsable_.end()) ? 0.0f : it->second;
if (now < ready) continue;

// after
if (!Get<SkillCooldownAgent>().IsReady(entry->skillId, now))
    continue;
```

파일 상단에 `#include "Agent/SkillCooldownAgent.h"` 추가.

- [ ] **Step 7: GamePacketHandler.cpp 수정**

line 210:
```cpp
// before
if (!player->TryConsumeCooldown(sk->sid, sk->cooldown))
    return Proto::ErrorCode::OK;

// after
if (!player->Get<SkillCooldownAgent>().TryConsume(sk->sid, sk->cooldown))
    return Proto::ErrorCode::OK;
```

상단에 `#include "Agent/SkillCooldownAgent.h"` 추가.

- [ ] **Step 8: MonsterStates.cpp 수정**

line 153:
```cpp
// before
owner.MarkSkillUsed(choice->skillId, now + choice->appliedCooldown);

// after
owner.Get<SkillCooldownAgent>().MarkUsed(choice->skillId, now + choice->appliedCooldown);
```

상단에 `#include "Agent/SkillCooldownAgent.h"` 추가.

- [ ] **Step 9: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`

잔재 확인: `grep -rn "TryConsumeCooldown\|MarkSkillUsed\|skillCooldowns_\|skillNextUsable_" mmosvr/GameServer/` → 비어 있어야 함.

- [ ] **Step 10: 런타임 스모크**

- Player: 같은 스킬 2 연타 → 두 번째는 쿨다운으로 무시(handler 의 조용히 return OK)
- Monster: 공격 후 쿨다운 동안 같은 스킬 재시전 안 함 (Engage 상태에서 대기 상태 관측)

- [ ] **Step 11: 커밋**

```bash
git add mmosvr/GameServer/Agent/SkillCooldownAgent.h mmosvr/GameServer/Agent/SkillCooldownAgent.cpp \
        mmosvr/GameServer/Unit.h mmosvr/GameServer/Player.h mmosvr/GameServer/Player.cpp \
        mmosvr/GameServer/Monster.h mmosvr/GameServer/Monster.cpp \
        mmosvr/GameServer/GamePacketHandler.cpp mmosvr/GameServer/MonsterStates.cpp \
        mmosvr/GameServer/GameServer.vcxproj mmosvr/GameServer/GameServer.vcxproj.filters
git commit -m "refactor(unit): SkillCooldownAgent 도입 — Player/Monster 쿨다운 통합"
```

---

## Task 4: FSMAgent 도입

**Files:**
- Create: `mmosvr/GameServer/Agent/FSMAgent.h`, `FSMAgent.cpp`
- Modify: `mmosvr/GameServer/Monster.h`, `Monster.cpp`
- Modify: `mmosvr/GameServer/MonsterStates.cpp`
- Modify: `mmosvr/GameServer/GameServer.vcxproj`, `.filters`

`MonsterFSM` 자체는 `FSM.h` 의 `StateMachine<Monster, MonsterStateId>` 템플릿 인스턴스화. 이 타입을 FSMAgent 내부 멤버로 옮긴다.

- [ ] **Step 1: FSMAgent.h 작성**

`mmosvr/GameServer/Agent/FSMAgent.h`:
```cpp
#pragma once

#include "Agent/IAgent.h"
#include "Agent/BuffAgent.h"
#include "MonsterStates.h"

class Monster;


// ===========================================================================
// FSMAgent — Monster 전용. MonsterFSM 소유 + state 관리 delegate.
// Tick 은 BuffAgent.CanAct() 체크 후 fsm_.Update 만.
// "Monster 에만 붙는다" 는 계약은 등록 지점(Monster::Monster()) 이 유일하다는
// 점으로 보장. ctor 의 static_cast<Monster&> 가 이 계약을 명문화한다.
// ===========================================================================
class FSMAgent : public IAgent
{
public:
    explicit FSMAgent(Unit& owner);

    void Tick(float deltaTime) override;

    // FSM 직접 접근 (상태 등록, OnStateChanged 콜백 등 초기 설정용).
    MonsterFSM&       GetFSM()       { return fsm_; }
    const MonsterFSM& GetFSM() const { return fsm_; }

    // 자주 쓰는 delegate shortcut.
    MonsterStateId GetCurrentStateId() const { return fsm_.GetCurrentStateId(); }
    void ChangeState(MonsterStateId s) { fsm_.ChangeState(s); }

private:
    Monster&   monsterOwner_;
    MonsterFSM fsm_;
};
```

- [ ] **Step 2: FSMAgent.cpp 작성**

`mmosvr/GameServer/Agent/FSMAgent.cpp`:
```cpp
#include "pch.h"
#include "Agent/FSMAgent.h"
#include "Monster.h"
#include "GameObject.h"
#include <cassert>


FSMAgent::FSMAgent(Unit& owner)
    : IAgent(owner)
    , monsterOwner_(static_cast<Monster&>(owner))
{
    // 계약 assert: FSMAgent 는 Monster 에만 등록된다.
    assert(owner.GetType() == GameObjectType::Monster);
}

void FSMAgent::Tick(const float deltaTime)
{
    // 총체적 행동 불가(Stun) 면 스킵. BuffAgent 는 이 Agent 보다 먼저 Tick 됨이 보장된다
    // (Unit ctor 에서 AddAgent<BuffAgent>() 먼저 호출됨).
    if (!owner_.Get<BuffAgent>().CanAct())
        return;
    fsm_.Update(deltaTime);
}
```

- [ ] **Step 3: vcxproj / filters 등록**

Agent/FSMAgent.h, .cpp 를 추가.

- [ ] **Step 4: Monster.h 에서 fsm_ 멤버 제거 + delegate 수정**

`mmosvr/GameServer/Monster.h` 수정:
```cpp
// 상단 include 에 추가
#include "Agent/FSMAgent.h"

// 공개 API: 기존 GetFSM / GetStateId 는 Agent 로 위임하도록 inline 구현 변경
// --- line 29 부근 (public) ---
MonsterFSM&       GetFSM()       { return Get<FSMAgent>().GetFSM(); }
const MonsterFSM& GetFSM() const { return Get<FSMAgent>().GetFSM(); }
MonsterStateId    GetStateId() const { return Get<FSMAgent>().GetCurrentStateId(); }

// --- private: line 87 부근의 `MonsterFSM fsm_;` 멤버 제거 ---
```

Monster 는 여전히 `GetFSM()` / `GetStateId()` 를 노출해 기존 호출처(MonsterStates 등)가 점진적으로 이동할 수 있게 한다 — 단, 다음 Step 에서 MonsterStates 는 `Get<FSMAgent>()` 로 직접 바꾼다. Monster 의 delegate 는 호출자 편의로 남긴다 (Monster 전용 Agent 이므로 Monster.h 에서 단축 제공하는 건 정합적).

- [ ] **Step 5: Monster.cpp InitAI 및 Update 수정**

`mmosvr/GameServer/Monster.cpp` InitAI (line 15~37):
```cpp
void Monster::InitAI(const Proto::Vector2& spawnPos)
{
    spawnPos_ = spawnPos;
    position_ = spawnPos;

    auto& fsm = Get<FSMAgent>().GetFSM();

    fsm.SetGlobalState<MonsterGlobalState>();

    fsm.AddState<IdleState>(MonsterStateId::Idle);
    fsm.AddState<PatrolState>(MonsterStateId::Patrol);
    fsm.AddState<EngageState>(MonsterStateId::Engage);
    fsm.AddState<ReturnState>(MonsterStateId::Return);

    fsm.SetOnStateChanged([this](MonsterStateId prev, MonsterStateId next)
        {
            BroadcastState(prev, next);
        });

    fsm.Start(*this, MonsterStateId::Idle);
}
```

Monster::Update (line 39~47) 는 이제 CanAct 체크를 FSMAgent 가 하므로:
```cpp
void Monster::Update(const float deltaTime)
{
    Unit::Update(deltaTime);
    // FSM 은 FSMAgent.Tick 에서 이미 실행됨. 추가 로직 없음.
}
```

사실 이 시점에서 Monster::Update 는 `Unit::Update` 만 호출하므로 override 자체를 제거하는 것도 가능하지만, 추후 Monster 고유 로직 추가 여지를 남겨 메서드는 유지.

- [ ] **Step 6: Monster ctor 에서 FSMAgent 등록**

`mmosvr/GameServer/Monster.h` ctor 수정:
```cpp
// before
Monster(std::string name, Zone& zone)
    : Npc(GameObjectType::Monster, zone, std::move(name))
{
}

// after
Monster(std::string name, Zone& zone)
    : Npc(GameObjectType::Monster, zone, std::move(name))
{
    AddAgent<FSMAgent>();
}
```

- [ ] **Step 7: MonsterStates.cpp 호출처 수정**

`mmosvr/GameServer/MonsterStates.cpp` 의 `owner.GetFSM().ChangeState(...)` 를 모두 `owner.Get<FSMAgent>().ChangeState(...)` 로 변경.

대상 위치 (이미 확인한 line 31, 70, 109, 141, 193):
```cpp
// before
owner.GetFSM().ChangeState(MonsterStateId::Engage);
// after
owner.Get<FSMAgent>().ChangeState(MonsterStateId::Engage);
```

5 군데 동일 패턴으로 치환. 상단에 `#include "Agent/FSMAgent.h"` 추가.

`owner.GetStateId()` (line 19) 는 Monster 의 delegate 가 여전히 Get<FSMAgent>() 로 위임하므로 수정 불필요 — 하지만 일관성을 위해 `owner.Get<FSMAgent>().GetCurrentStateId()` 로 변경 권장. (둘 다 동작한다. 변경 권장.)

- [ ] **Step 8: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`

주의점:
- Monster.h 에 `#include "Agent/FSMAgent.h"` 를 추가했기 때문에 include 순환(FSMAgent → Monster) 가능. FSMAgent.h 는 Monster 전방 선언만, cpp 에서 Monster.h include. FSMAgent.cpp 는 Monster.h 를 include 하므로 OK.
- Monster.h 의 `#include "Agent/FSMAgent.h"` 때문에 FSMAgent.h 가 MonsterStates.h 를 include 하면 순환. FSMAgent.h 는 MonsterStates.h 만 include하고 Monster.h 는 include 안 함 (전방 선언으로 충분).

- [ ] **Step 9: 런타임 스모크**

- 몬스터 Idle → Patrol → Idle 순환
- 플레이어 접근 시 Engage → 공격 → leash 벗어나면 Return → Idle
- Stun 스킬로 몬스터 묶으면 FSM 정지 (버프 만료 후 재개)

- [ ] **Step 10: 커밋**

```bash
git add mmosvr/GameServer/Agent/FSMAgent.h mmosvr/GameServer/Agent/FSMAgent.cpp \
        mmosvr/GameServer/Monster.h mmosvr/GameServer/Monster.cpp \
        mmosvr/GameServer/MonsterStates.cpp \
        mmosvr/GameServer/GameServer.vcxproj mmosvr/GameServer/GameServer.vcxproj.filters
git commit -m "refactor(monster): FSMAgent 도입 — FSM 을 Agent 로 이관"
```

---

## Task 5: AggroAgent 도입

**Files:**
- Create: `mmosvr/GameServer/Agent/AggroAgent.h`, `AggroAgent.cpp`
- Modify: `mmosvr/GameServer/Monster.h`, `Monster.cpp`
- Modify: `mmosvr/GameServer/MonsterStates.cpp`
- Modify: `mmosvr/GameServer/Projectile.cpp`
- Modify: `mmosvr/GameServer/GameServer.vcxproj`, `.filters`

- [ ] **Step 1: AggroAgent.h 작성**

`mmosvr/GameServer/Agent/AggroAgent.h`:
```cpp
#pragma once

#include "Agent/IAgent.h"
#include "AggroTable.h"
#include "Utils/Types.h"


// ===========================================================================
// AggroAgent — Monster 전용. AggroTable 소유 + delegate.
// Add / ResolveTop / Clear / Empty 네 가지 핵심 API 만 노출.
// ===========================================================================
class AggroAgent : public IAgent
{
public:
    explicit AggroAgent(Unit& owner) : IAgent(owner) {}

    void Add(long long playerGuid, float amount) { table_.Add(playerGuid, amount); }
    long long GetTop() const { return table_.ResolveTop(); }  // 없으면 0
    bool HasAny() const { return !table_.Empty(); }
    void Clear() { table_.Clear(); }

private:
    AggroTable table_;
};
```

AggroTable 멤버 함수명(`Add`, `ResolveTop`, `Clear`, `Empty`)은 현 코드 그대로. Agent API 는 호출측 가독성을 위해 `GetTop` / `HasAny` 로 짧게 노출.

- [ ] **Step 2: AggroAgent.cpp — 구현 없음 (헤더에 전부 inline)**

Agent 의 본체가 AggroTable 위임이라 별도 .cpp 가 필요 없을 수도 있지만, 프로젝트 컨벤션상 cpp 파일을 쓰는 편이 vcxproj 관리에 일관적. 빈 번역 단위:
```cpp
// mmosvr/GameServer/Agent/AggroAgent.cpp
#include "pch.h"
#include "Agent/AggroAgent.h"
// 구현 없음 — AggroAgent 는 header-only delegate.
```

- [ ] **Step 3: vcxproj / filters 등록**

Agent/AggroAgent.h, .cpp 추가.

- [ ] **Step 4: Monster.h / Monster.cpp 에서 Aggro delegate 제거 + ctor 등록**

`mmosvr/GameServer/Monster.h`:
- line 43~46 의 4 선언 제거:
  ```cpp
  void AddAggro(long long playerGuid, float amount);
  long long GetTopAggroGuid() const;
  bool HasAggro() const;
  void ClearAggro();
  ```
- line 102 의 `AggroTable aggro_;` 멤버 제거
- 상단에 `#include "Agent/AggroAgent.h"` 추가
- ctor 에 `AddAgent<AggroAgent>();` 추가 (Monster 전용 Agent; FSMAgent 뒤에):
```cpp
Monster(std::string name, Zone& zone)
    : Npc(GameObjectType::Monster, zone, std::move(name))
{
    AddAgent<FSMAgent>();
    AddAgent<AggroAgent>();
}
```

`mmosvr/GameServer/Monster.cpp`:
- line 53~71 의 4 구현(AddAggro / GetTopAggroGuid / HasAggro / ClearAggro) 전부 제거

- [ ] **Step 5: MonsterStates.cpp 호출처 수정**

대상 line:
- 25: `if (owner.HasAggro())` → `if (owner.Get<AggroAgent>().HasAny())`
- 27: `owner.GetTopAggroGuid()` → `owner.Get<AggroAgent>().GetTop()`
- 42: `owner.AddAggro(player->GetGuid(), 0)` → `owner.Get<AggroAgent>().Add(player->GetGuid(), 0)`
- 129: `owner.GetTopAggroGuid()` → `owner.Get<AggroAgent>().GetTop()`
- 180: `owner.ClearAggro()` → `owner.Get<AggroAgent>().Clear()`

상단에 `#include "Agent/AggroAgent.h"` 추가.

- [ ] **Step 6: Projectile.cpp 호출처 수정**

line 66:
```cpp
// before
monster.AddAggro(ownerGuid_, static_cast<float>(actualDmg));
// after
monster.Get<AggroAgent>().Add(ownerGuid_, static_cast<float>(actualDmg));
```

상단에 `#include "Agent/AggroAgent.h"` 추가.

- [ ] **Step 7: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Build succeeded. 0 Error(s)`

잔재 확인: `grep -rn "AddAggro\b\|GetTopAggroGuid\|HasAggro\|ClearAggro\|aggro_\b" mmosvr/GameServer/` → 비어 있어야 함.

- [ ] **Step 8: 런타임 스모크**

- 플레이어가 몬스터 공격 → 어그로 쌓임 → 몬스터 Engage
- 다른 플레이어가 더 높은 데미지 가하면 타겟 전환 (top aggro 재계산이 EngageState::OnUpdate 에서 잘 동작)
- Leash 탈출 → Return → ClearAggro 후 Idle

- [ ] **Step 9: 커밋**

```bash
git add mmosvr/GameServer/Agent/AggroAgent.h mmosvr/GameServer/Agent/AggroAgent.cpp \
        mmosvr/GameServer/Monster.h mmosvr/GameServer/Monster.cpp \
        mmosvr/GameServer/MonsterStates.cpp mmosvr/GameServer/Projectile.cpp \
        mmosvr/GameServer/GameServer.vcxproj mmosvr/GameServer/GameServer.vcxproj.filters
git commit -m "refactor(monster): AggroAgent 도입 — AggroTable 을 Agent 로 이관"
```

---

## 전체 검증 (Task 5 완료 후)

모든 Task 가 끝난 시점에서 다음을 최종 확인:

- [ ] **grep 잔재 확인**: 아래 명령이 모두 결과 0 이어야 한다.
  ```bash
  grep -rn "BuffContainer" mmosvr/GameServer/
  grep -rn "buffs_\." mmosvr/GameServer/
  grep -rn "->CanMove()\|->CanAct()\|->CanAttack()\|->CanCastSkill()\|->CanIgnoreDamage()" mmosvr/GameServer/
  grep -rn "->ApplyEffect(\|->DispelBuff(\|->TickBuffs(\|->GetEffectiveMoveSpeed(" mmosvr/GameServer/
  grep -rn "TryConsumeCooldown\|MarkSkillUsed\|skillCooldowns_\|skillNextUsable_" mmosvr/GameServer/
  grep -rn "AddAggro\b\|GetTopAggroGuid\|HasAggro\|ClearAggro\|aggro_\b" mmosvr/GameServer/
  ```

- [ ] **빌드 Release 도 확인**:
  ```
  msbuild mmosvr.sln /p:Configuration=Release /p:Platform=x64 /m
  ```

- [ ] **End-to-end 런타임**: 로그인 → 이동 → 스킬 시전 → 몬스터 공격 → 버프(stun/slow) 적용 → 만료 → 몬스터 사망/Return.

- [ ] **debug_tool 로 CC 검증**: `/spawn` 으로 몬스터 소환, `/stun` / `/silence` / `/root` / `/invul` 각 버프 적용 → 해당 CC 가 FSM 과 이동/스킬/평타에 정확히 반영되는지.

---

## Appendix: 각 Task 종료 후 롤백

각 Task 는 단일 커밋으로 끝난다. 문제 발생 시:
```bash
git revert <커밋해시>
```
로 되돌린다. Task 간 의존성은:
- Task 2 는 Task 1 의존
- Task 3, 4, 5 는 서로 독립 (Task 2 후에는 순서 무관) — 필요 시 개별 revert 가능.
