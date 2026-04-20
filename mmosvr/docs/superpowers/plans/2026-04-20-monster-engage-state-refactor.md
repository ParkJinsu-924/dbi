# Monster Engage State Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `MonsterStates` FSM 의 `Chase`/`Attack` 을 `Engage` 단일 상태로 통합하고, 스킬 행동을 `ISkillBehavior` Strategy 로 분리해 AI 코드를 건드리지 않고 특수 스킬을 추가할 수 있도록 한다.

**Architecture:** FSM 은 유지하되 "전투 단계를 하나의 Engage 상태로 통합" + "스킬이 자기 행동을 담아 AI 는 호출만 수행" 의 두 축. `EngageState` 내부에 `Phase { Approach, Casting, Waiting }` 관측 태그를 두어 디버그 가시성을 유지한다.

**Tech Stack:** C++20, MSBuild (VS 2022), 기존 `FSM.h`, `ResourceManager`, `SkillRuntime`, CSV 기반 데이터 테이블.

**Spec:** `mmosvr/docs/superpowers/specs/2026-04-20-monster-engage-state-refactor-design.md`

**검증 주의:** 이 프로젝트엔 단위 테스트 프레임워크가 없다. 각 task 의 "verify" 단계는 (a) 빌드 성공 (b) `DummyClient` 로 교전 관찰 의 조합이다. TDD 의 "failing test first" 는 해당 없음 — 대신 "변경 전 동작을 DummyClient 로 기준 녹화 → 변경 후 동일 동작 재현" 원칙을 따른다.

**Build command (모든 task 의 빌드 검증에 공통):**
```bash
msbuild /c/Users/qkrwlstn924/Desktop/dbi/mmosvr/mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m
```
(VS 2022 Developer Command Prompt 또는 동등 환경)

---

## File Structure

**신규 파일:**
- `mmosvr/GameServer/SkillBehavior.h` — `ISkillBehavior` 추상 베이스
- `mmosvr/GameServer/SkillBehavior.cpp` — `DefaultAttackBehavior::Execute` 구현
- `mmosvr/GameServer/SkillBehaviorRegistry.h` — 이름→factory 레지스트리 (inline)

**수정 파일:**
- `mmosvr/GameServer/GameServer.vcxproj` — 신규 파일 등록
- `mmosvr/GameServer/SkillTemplate.h` — `behaviorName` 필드 + `SkillTable::OnLoaded` 에서 행동 주입
- `ShareDir/data/skill_templates.csv` — `behavior` 컬럼 추가
- `mmosvr/GameServer/ResourceManager.cpp` — 부팅 시 default behavior 등록
- `mmosvr/GameServer/MonsterStates.h` — `Chase`/`Attack` 제거, `Engage` 추가
- `mmosvr/GameServer/MonsterStates.cpp` — 상태 구현 교체
- `mmosvr/GameServer/Monster.h` — `DoAttack` 제거, `GetTargetingPhase` 추가 (선택)
- `mmosvr/GameServer/Monster.cpp` — `InitAI` 의 상태 등록 업데이트, `DoAttack` 내용 이동, `PickCastable` 에 `CanCast` 추가
- `ShareDir/proto/game.proto` — `S_MonsterState.state` 주석 업데이트

---

## Task 1: `ISkillBehavior` 헤더 작성

**Files:**
- Create: `mmosvr/GameServer/SkillBehavior.h`

- [ ] **Step 1: 헤더 파일 생성**

파일 내용 전체:

```cpp
#pragma once

// Strategy 인터페이스 — 스킬마다의 "AI 수준 행동" 을 캡슐화한다.
// 데미지/Effect 적용 같은 실제 처리는 SkillRuntime::Cast 가 담당하며,
// Behavior 는 그 호출 여부 및 주변 처리(예: 추가 조건, 선행 이동 등)를 결정한다.
// 기본 구현은 DefaultAttackBehavior — SkillRuntime::Cast 한 번 호출만.

#include <string>

class Monster;
class Player;
struct SkillTemplate;


class ISkillBehavior
{
public:
	virtual ~ISkillBehavior() = default;

	// 쿨다운·사거리 외 추가 시전 조건. 기본은 항상 true.
	virtual bool CanCast(const Monster& /*owner*/, const Player& /*target*/, float /*now*/) const
	{
		return true;
	}

	// 시전 실행. 데미지 적용/패킷 브로드캐스트 등.
	// now 는 TimeManager.totalTime 기준 (PickCastable 과 동일 clock).
	virtual void Execute(const SkillTemplate& skill, Monster& owner, Player& target, float now) = 0;

	// 0 = 즉발. 향후 선딜 스킬이 필요해지면 이 값이 사용된다 (이번 리팩토링 범위 밖).
	virtual float GetCastTime() const { return 0.0f; }
};


// 현재 Monster::DoAttack 의 로직을 그대로 담는 기본 구현. 구현은 .cpp.
class DefaultAttackBehavior : public ISkillBehavior
{
public:
	void Execute(const SkillTemplate& skill, Monster& owner, Player& target, float now) override;
};
```

- [ ] **Step 2: 빌드가 깨지지 않는지 확인 (헤더만 있어 아직 link 없음)**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 이 파일은 아직 어디서도 `#include` 되지 않았으므로 빌드 변화 없음. 성공.

- [ ] **Step 3: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/SkillBehavior.h
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): add ISkillBehavior interface"
```

---

## Task 2: `DefaultAttackBehavior::Execute` 구현

**Files:**
- Create: `mmosvr/GameServer/SkillBehavior.cpp`

- [ ] **Step 1: .cpp 파일 생성**

파일 내용 전체:

```cpp
#include "pch.h"
#include "SkillBehavior.h"
#include "Monster.h"
#include "Player.h"
#include "SkillTemplate.h"
#include "SkillRuntime.h"


void DefaultAttackBehavior::Execute(const SkillTemplate& skill, Monster& owner, Player& target, float /*now*/)
{
	// 기존 Monster::DoAttack 과 동일:
	//  - Stun 시 공격 불가. (Silence 는 "기본 공격" 이라 면제 — LoL 관습 유지)
	//  - SkillRuntime::Cast 로 targeting 별 디스패치 위임.
	if (!owner.CanAttack()) return;
	SkillRuntime::Cast(skill, owner, target, owner.GetZone());
}
```

**주의:** `Monster::CanAttack` 은 현재 Unit/Npc 계통에서 상속. `owner.CanAttack()` 가 public 인지 확인 (Monster.cpp 의 기존 `DoAttack` 이 이미 `CanAttack()` 를 호출 중이므로 접근 가능).

- [ ] **Step 2: Commit (아직 vcxproj 에 안 붙였으므로 빌드엔 미포함)**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/SkillBehavior.cpp
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): add DefaultAttackBehavior implementation"
```

---

## Task 3: `SkillBehaviorRegistry` 추가

**Files:**
- Create: `mmosvr/GameServer/SkillBehaviorRegistry.h`

- [ ] **Step 1: 헤더 작성 (inline, .cpp 불필요)**

파일 내용 전체:

```cpp
#pragma once

#include "SkillBehavior.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>


// 이름 → Behavior 인스턴스 팩토리.
// 부팅 시 1회 Register 하고, SkillTable::OnLoaded 에서 Create 해 SkillTemplate 에 바인딩한다.
// 등록은 I/O 스레드 시작 전(main::Init) 에만 이루어지므로 락 불필요.
// shared_ptr 을 쓰는 이유: 같은 behavior 인스턴스를 여러 SkillTemplate 이 공유 가능 (상태 없음).

class SkillBehaviorRegistry
{
public:
	using Factory = std::function<std::shared_ptr<ISkillBehavior>()>;

	static SkillBehaviorRegistry& Instance()
	{
		static SkillBehaviorRegistry inst;
		return inst;
	}

	void Register(std::string name, Factory factory)
	{
		factories_[std::move(name)] = std::move(factory);
	}

	// 미등록 이름 또는 빈 문자열이면 "default" 로 폴백. "default" 도 없으면 nullptr.
	std::shared_ptr<ISkillBehavior> Create(const std::string& name) const
	{
		const std::string& key = name.empty() ? kDefault : name;
		const auto it = factories_.find(key);
		if (it == factories_.end())
		{
			const auto fallback = factories_.find(kDefault);
			return fallback != factories_.end() ? fallback->second() : nullptr;
		}
		return it->second();
	}

private:
	SkillBehaviorRegistry() = default;
	static inline const std::string kDefault = "default";
	std::unordered_map<std::string, Factory> factories_;
};
```

- [ ] **Step 2: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/SkillBehaviorRegistry.h
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): add SkillBehaviorRegistry"
```

---

## Task 4: `GameServer.vcxproj` 에 신규 파일 등록

**Files:**
- Modify: `mmosvr/GameServer/GameServer.vcxproj`

- [ ] **Step 1: ClCompile / ClInclude 추가**

`mmosvr/GameServer/GameServer.vcxproj` 의 `ClCompile` 섹션 끝(107라인 근처 `SkillshotProjectile.cpp` 바로 아래)에 추가:

```xml
        <ClCompile Include="SkillBehavior.cpp"/>
```

`ClInclude` 섹션에 추가 (적당한 인접 그룹 끝, 예: `SkillTemplate.h` 근처):

```xml
        <ClInclude Include="SkillBehavior.h"/>
        <ClInclude Include="SkillBehaviorRegistry.h"/>
```

정확한 위치를 알려면:

```bash
grep -n "SkillshotProjectile" /c/Users/qkrwlstn924/Desktop/dbi/mmosvr/GameServer/GameServer.vcxproj
```

- [ ] **Step 2: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 성공. `SkillBehavior.cpp` 가 이제 컴파일 대상이 됨. 아직 어디서도 호출하지 않지만 `DefaultAttackBehavior::Execute` 가 참조하는 `Monster::CanAttack`, `Monster::GetZone`, `SkillRuntime::Cast` 가 해석되어야 한다. 실패 시 include 누락 체크.

- [ ] **Step 3: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/GameServer.vcxproj
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "build: register SkillBehavior sources in GameServer.vcxproj"
```

---

## Task 5: `SkillTemplate` 에 `behaviorName` + `behavior` 필드 추가

**Files:**
- Modify: `mmosvr/GameServer/SkillTemplate.h`

- [ ] **Step 1: 필드와 `CSV_DEFINE_TYPE` 업데이트**

기존 `SkillTemplate.h` 의 struct 정의를 아래로 교체:

```cpp
#pragma once

#include "ResourceManager.h"
#include "AttackTypes.h"
#include "SkillBehavior.h"
#include <memory>
#include <string>


// Phase 1: 스킬 메타데이터만 보관. 데미지/버프 등의 "효과" 는 Effect + SkillEffectEntry 에 위임.
// targeting 은 SkillKind enum 값 재사용 (Homing=0, Skillshot=1 — Proto::ProjectileKind 와 값 일치 유지).

class SkillTable;

struct SkillTemplate
{
	using KeyType = int32;
	using Table = SkillTable;

	int32       sid                 = 0;
	std::string name;
	SkillKind   targeting           = SkillKind::Homing;
	float       projectile_speed    = 10.0f;
	float       projectile_radius   = 0.5f;   // Skillshot 충돌 반경
	float       projectile_range    = 0.0f;   // Skillshot 사거리
	float       projectile_lifetime = 5.0f;   // Homing 생존시간 안전장치
	float       cooldown            = 1.0f;
	int32       cost                = 0;      // 자원 소모 (Phase 2+)
	float       cast_range          = 30.0f;  // 서버측 사거리 validation (Phase 2+)

	// AI 행동 이름. "" 또는 "default" → DefaultAttackBehavior. 특수 스킬은 이 필드로 식별.
	std::string behaviorName;

	// 로드 후 SkillTable::OnLoaded 에서 주입. Runtime 에서는 항상 non-null.
	// mutable: CSV 로드 후 SkillTable 이 채워 넣지만, 소비자는 const 뷰로 접근.
	mutable std::shared_ptr<ISkillBehavior> behavior;

	KeyType GetKey() const { return sid; }

	CSV_DEFINE_TYPE(SkillTemplate,
		sid, name, targeting,
		projectile_speed, projectile_radius, projectile_range, projectile_lifetime,
		cooldown, cost, cast_range, behaviorName)
};


class SkillTable : public KeyedResourceTable<SkillTemplate>
{
protected:
	void OnLoaded() override;   // 구현은 SkillTemplate.cpp (신규) — behavior 바인딩.
};
```

- [ ] **Step 2: `SkillTable::OnLoaded` 구현 파일 생성**

**Files:**
- Create: `mmosvr/GameServer/SkillTemplate.cpp`

내용:

```cpp
#include "pch.h"
#include "SkillTemplate.h"
#include "SkillBehaviorRegistry.h"


void SkillTable::OnLoaded()
{
	auto& registry = SkillBehaviorRegistry::Instance();
	int missing = 0;

	for (auto& [sid, tmpl] : map_)
	{
		tmpl.behavior = registry.Create(tmpl.behaviorName);
		if (!tmpl.behavior)
		{
			LOG_ERROR(std::format(
				"SkillTable: sid={} — no behavior registered for name '{}' and no 'default' fallback",
				sid, tmpl.behaviorName));
			++missing;
		}
	}

	if (missing > 0)
	{
		// SkillBehaviorRegistry 에 'default' 가 등록되어 있으면 여기 도달 불가.
		// main::Init 순서 버그 시에만 발생 — 부팅 중단이 안전.
		throw std::runtime_error("SkillTable: behavior binding failed — check SkillBehaviorRegistry init order");
	}
}
```

**주의:** `map_` 은 `KeyedResourceTable::map_` protected 멤버로 `mutable` 이 아니므로 `OnLoaded` 안에서 `tmpl.behavior` 쓰기가 됩니다 (`map_` 의 value 자체는 비 const).

- [ ] **Step 3: vcxproj 에 `SkillTemplate.cpp` 추가**

`GameServer.vcxproj` 의 `ClCompile` 섹션에:

```xml
        <ClCompile Include="SkillTemplate.cpp"/>
```

- [ ] **Step 4: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 성공해야 하지만 아직 skill_templates.csv 가 `behaviorName` 컬럼을 갖고 있지 않으므로 **런타임 로드 시점**엔 `behaviorName` 이 빈 문자열로 파싱될 것. 이건 Task 6 에서 해결. 컴파일만 통과하면 OK.

또한 `SkillBehaviorRegistry` 에 "default" 가 아직 register 되지 않았으므로 이 상태에서 서버 구동 시 boot 실패 (`throw runtime_error`). 정상 — Task 7 에서 해결.

- [ ] **Step 5: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/SkillTemplate.h mmosvr/GameServer/SkillTemplate.cpp mmosvr/GameServer/GameServer.vcxproj
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(skill): bind ISkillBehavior to SkillTemplate via registry"
```

---

## Task 6: `skill_templates.csv` 에 `behavior` 컬럼 추가

**Files:**
- Modify: `ShareDir/data/skill_templates.csv`

- [ ] **Step 1: 헤더와 모든 행 업데이트**

기존:

```
sid,name,targeting,projectile_speed,projectile_radius,projectile_range,projectile_lifetime,cooldown,cost,cast_range
1001,goblin_auto,Melee,0.0,0.0,0.0,0.0,1.2,0,2.0
...
```

다음으로 교체:

```
# Phase 1: 스킬 메타데이터만 보관. 데미지/버프 등의 "효과"는 effects.csv + skill_effects.csv 로 분리.
# targeting: Melee | Hitscan | Homing | Skillshot  (대소문자 무관, 숫자도 허용 — 0=Melee, 1=Hitscan, 2=Homing, 3=Skillshot)
# projectile_* : 투사체 파라미터. Melee/Hitscan 은 0. Homing 은 speed/lifetime, Skillshot 은 speed/radius/range 사용.
# cost       : 자원 소모 (마나 등). Phase 1 은 0.
# cast_range : 사거리. Melee/Hitscan 은 공격 리치, Homing/Skillshot 은 서버측 validation.
# cooldown   : 스킬 재사용 대기 (평타의 attack speed 역할 포함).
# behavior   : AI 수준 스킬 행동. 빈 값/"default" → DefaultAttackBehavior (현재 유일한 구현).
#              특수 스킬을 추가할 때 ISkillBehavior 상속 클래스를 만들고 이름을 여기에 적는다.
sid,name,targeting,projectile_speed,projectile_radius,projectile_range,projectile_lifetime,cooldown,cost,cast_range,behavior
1001,goblin_auto,Melee,0.0,0.0,0.0,0.0,1.2,0,2.0,default
1002,orc_auto,Melee,0.0,0.0,0.0,0.0,2.0,0,2.5,default
1003,slime_auto,Melee,0.0,0.0,0.0,0.0,1.0,0,1.5,default
1004,sniper_shot,Hitscan,0.0,0.0,0.0,0.0,2.0,0,10.0,default
1005,archer_homing,Homing,12.0,0.0,0.0,5.0,1.8,0,8.0,default
1006,caster_bolt,Skillshot,18.0,0.6,16.0,0.0,2.5,0,10.0,default
# Goblin 스페셜 스킬: 큰 폭탄을 던진다. 느리지만 큰 피탄 반경 + 슬로우 부여 (skill_effects 참조)
1101,goblin_bomb,Skillshot,8.0,1.5,8.0,0.0,4.0,0,8.0,default
2001,auto_attack,Homing,12.0,0.0,0.0,5.0,0.0,0,30.0,default
2002,bolt,Skillshot,18.0,0.6,16.0,0.0,0.0,0,20.0,default
2003,strike,Homing,14.0,0.0,0.0,3.0,0.0,0,25.0,default
2004,nuke,Skillshot,22.0,1.2,24.0,0.0,0.0,0,30.0,default
```

- [ ] **Step 2: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add ShareDir/data/skill_templates.csv
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "data(skill): add behavior column (all default)"
```

---

## Task 7: 부팅 시 `DefaultAttackBehavior` 등록

**Files:**
- Modify: `mmosvr/GameServer/ResourceManager.cpp`

- [ ] **Step 1: Init 시작부에 register 호출 추가**

`ResourceManager::Init()` 수정. `SkillTable` 로딩 **이전에** register 되어야 `OnLoaded` 에서 바인딩 실패하지 않는다.

```cpp
#include "SkillBehavior.h"
#include "SkillBehaviorRegistry.h"
```

추가 후, `Init` 함수 맨 앞:

```cpp
void ResourceManager::Init()
{
	// Behavior 레지스트리는 SkillTable 로딩보다 먼저 등록되어야 한다.
	// SkillTable::OnLoaded 가 각 SkillTemplate.behaviorName → Behavior 를 조회하므로.
	SkillBehaviorRegistry::Instance().Register("default",
		[] { return std::make_shared<DefaultAttackBehavior>(); });

	Register<MonsterTemplate>("monster_templates.csv");
	// ... (이하 기존 코드 그대로)
```

- [ ] **Step 2: 빌드 + 서버 부팅 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 빌드 성공.

`bin/Debug/x64/GameServer.exe` 실행 후 로그 확인:
- `ResourceManager: Loaded N entries from skill_templates.csv` 출력되는지
- `SkillTable: sid=... — no behavior registered` 에러가 **없어야** 함

현재 아무 기능도 실질적으로 사용하지 않지만(아직 Monster::DoAttack 을 바꾸지 않음), 서버가 정상 구동되면 바인딩은 성공한 것.

- [ ] **Step 3: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/ResourceManager.cpp
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "init(skill): register DefaultAttackBehavior at boot"
```

---

## Task 8: `Monster::PickCastable` 에 `CanCast` 체크 추가

**Files:**
- Modify: `mmosvr/GameServer/Monster.cpp`

- [ ] **Step 1: 후보 필터에 `behavior->CanCast` 추가**

`PickCastable` 안의 후보 수집 루프 수정. 기존:

```cpp
for (const auto* entry : entries)
{
    const SkillTemplate* tmpl = skTable->Find(entry->skillId);
    if (!tmpl) continue;
    if (distance > tmpl->cast_range) continue;

    const auto it = skillNextUsable_.find(entry->skillId);
    const float ready = (it == skillNextUsable_.end()) ? 0.0f : it->second;
    if (now < ready) continue;

    const float applied = (std::max)(tmpl->cooldown, entry->minInterval);
    candidates.push_back({ entry, tmpl, applied });
    totalWeight += entry->weight;
}
```

를 다음으로 변경:

```cpp
auto target = GetTarget();   // CanCast 는 target 이 필요. 없으면 후보 전체 탈락.
if (!target) return std::nullopt;

for (const auto* entry : entries)
{
    const SkillTemplate* tmpl = skTable->Find(entry->skillId);
    if (!tmpl) continue;
    if (distance > tmpl->cast_range) continue;

    const auto it = skillNextUsable_.find(entry->skillId);
    const float ready = (it == skillNextUsable_.end()) ? 0.0f : it->second;
    if (now < ready) continue;

    // Strategy: 스킬별 추가 조건 (HP threshold, stance 등). Default 는 항상 true.
    if (tmpl->behavior && !tmpl->behavior->CanCast(*this, *target, now)) continue;

    const float applied = (std::max)(tmpl->cooldown, entry->minInterval);
    candidates.push_back({ entry, tmpl, applied });
    totalWeight += entry->weight;
}
```

- [ ] **Step 2: 빌드 + 서버 부팅 + DummyClient 로 1마리 유인 교전 관찰**

Expected: 기존과 동일한 교전 행동 (default CanCast 는 항상 true 이므로 필터 무효). 빌드 성공, DummyClient 의 몬스터가 평소처럼 공격함.

- [ ] **Step 3: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/Monster.cpp
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): filter PickCastable candidates by behavior.CanCast"
```

---

## Task 9: `EngageState` 클래스 추가 + `MonsterStateId::Engage` 정의

**Files:**
- Modify: `mmosvr/GameServer/MonsterStates.h`

- [ ] **Step 1: enum 과 클래스 선언 추가**

`MonsterStates.h` 를 다음으로 전체 교체:

```cpp
#pragma once

#include "FSM.h"

class Monster;

enum class MonsterStateId : uint8_t
{
	Idle,
	Patrol,
	Engage,
	Return
};

using MonsterFSM = StateMachine<Monster, MonsterStateId>;


// ---------------------------------------------------------------------------
// GlobalDetectState — every tick, detect player within range -> Engage.
// Idle/Patrol 상태에서만 감지 수행. 이미 교전(Engage)/귀환(Return) 중이면 스킵.
// ---------------------------------------------------------------------------
class MonsterGlobalState : public IState<Monster>
{
public:
	void OnUpdate(Monster& owner, float deltaTime) override;
};


// ---------------------------------------------------------------------------
// Idle — spawn point standby
// ---------------------------------------------------------------------------
class IdleState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;
	void OnExit(Monster& owner) override;

private:
	float idleTime_ = 0.0f;
};


// ---------------------------------------------------------------------------
// Patrol — move to a random point within range of spawn, then go Idle
// ---------------------------------------------------------------------------
class PatrolState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

private:
	float targetX_ = 0.0f;
	float targetZ_ = 0.0f;
};


// ---------------------------------------------------------------------------
// Engage — 타겟 획득 후 전투 행동 전체 (접근 + 시전 + 대기).
// 매 틱 PickCastable 결과로 "시전 가능하면 시전, 아니면 거리 기준 접근/대기" 분기.
// phase_ 는 로직에 영향 없는 관측 태그 — 디버그·분석용.
// ---------------------------------------------------------------------------
class EngageState : public IState<Monster>
{
public:
	enum class Phase : uint8_t { Approach, Casting, Waiting };

	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;

	Phase GetPhase() const { return phase_; }

private:
	Phase phase_ = Phase::Approach;
};


// ---------------------------------------------------------------------------
// Return — 스폰 지점으로 복귀
// ---------------------------------------------------------------------------
class ReturnState : public IState<Monster>
{
public:
	void OnEnter(Monster& owner) override;
	void OnUpdate(Monster& owner, float deltaTime) override;
};
```

- [ ] **Step 2: 커밋 (아직 .cpp 에서 참조하지 않아 링크 에러 없음, 빌드 실패 예상)**

실제로 이 시점에 `MonsterStates.cpp` 는 여전히 `ChaseState::OnUpdate`, `AttackState::OnUpdate` 를 정의하고 있고, enum 에서 `Chase`, `Attack` 이 빠졌으므로 **빌드는 깨진다**. 이건 Task 10 에서 해결. 커밋만 먼저:

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/MonsterStates.h
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): replace Chase/Attack with Engage in MonsterStateId [WIP]"
```

---

## Task 10: `MonsterStates.cpp` 를 Engage 구현으로 교체

**Files:**
- Modify: `mmosvr/GameServer/MonsterStates.cpp`

- [ ] **Step 1: 파일 전체 교체**

```cpp
#include "pch.h"
#include "MonsterStates.h"
#include "Monster.h"
#include "Zone.h"
#include "Player.h"
#include "SkillTemplate.h"
#include "SkillBehavior.h"
#include <cmath>
#include <random>
#include "PacketMaker.h"


// ===========================================================================
// GlobalDetectState — runs every tick before current state
// ===========================================================================

void MonsterGlobalState::OnUpdate(Monster& owner, float /*deltaTime*/)
{
	switch (owner.GetStateId())
	{
	case MonsterStateId::Idle:
	case MonsterStateId::Patrol:
	{
		// 이미 누적된 aggro 가 있으면 top 대상으로 Engage 진입.
		if (owner.HasAggro())
		{
			const long long topGuid = owner.GetTopAggroGuid();
			if (topGuid != 0)
			{
				owner.SetTarget(topGuid);
				owner.GetFSM().ChangeState(MonsterStateId::Engage);
				break;
			}
		}

		{ // 가까운 Player 탐지 시 Aggro 세팅
			const auto player = owner.GetZone().FindNearestPlayer(
				owner.GetPosition(), owner.GetDetectRange());

			if (player)
			{
				owner.AddAggro(player->GetGuid(), 0); // Aggro 최소치 세팅
			}
		}
	}
	break;
	case MonsterStateId::Engage:
	case MonsterStateId::Return:
	default:
		break;
	}
}


// ===========================================================================
// Idle
// ===========================================================================

void IdleState::OnEnter(Monster& owner)
{
	owner.ClearTarget();
	idleTime_ = 0.0f;
}

void IdleState::OnUpdate(Monster& owner, const float deltaTime)
{
	idleTime_ += deltaTime;
	if (idleTime_ >= 4.0f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Patrol);
		return;
	}
}

void IdleState::OnExit(Monster& /*owner*/)
{
	idleTime_ = 0.0f;
}


// ===========================================================================
// Patrol — move to a random point within range of spawn, then go Idle
// ===========================================================================

void PatrolState::OnEnter(Monster& owner)
{
	constexpr float PATROL_RANGE = 5.0f;

	static thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_real_distribution<float> angleDist(0.0f, 6.28318530718f);
	std::uniform_real_distribution<float> radiusDist(0.0f, PATROL_RANGE);

	const float angle = angleDist(rng);
	const float radius = radiusDist(rng);
	const auto& spawn = owner.GetSpawnPos();

	targetX_ = spawn.x() + radius * std::cos(angle);
	targetZ_ = spawn.y() + radius * std::sin(angle);
}

void PatrolState::OnUpdate(Monster& owner, const float deltaTime)
{
	Proto::Vector2 target;
	target.set_x(targetX_);
	target.set_y(targetZ_);

	if (owner.DistanceTo(target) <= 0.3f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(target, deltaTime);
}


// ===========================================================================
// Engage — 추격 + 시전 + 대기 통합. Chase/Attack 은 더 이상 없다.
// ===========================================================================

void EngageState::OnEnter(Monster& /*owner*/)
{
	phase_ = Phase::Approach;
}

void EngageState::OnUpdate(Monster& owner, const float deltaTime)
{
	// 1) 매 틱 top aggro 재계산, 현재 target 과 다르면 교체.
	const long long topGuid = owner.GetTopAggroGuid();
	if (topGuid != 0)
	{
		const auto cur = owner.GetTarget();
		if (cur == nullptr || cur->GetGuid() != topGuid)
			owner.SetTarget(topGuid);
	}

	// 2) 종료 조건: 타겟 소실 / leash 초과 → Return.
	const auto target = owner.GetTarget();
	if (!target || !target->IsAlive() || owner.DistanceToSpawn() > owner.GetLeashRange())
	{
		owner.GetFSM().ChangeState(MonsterStateId::Return);
		return;
	}

	// 3) 시전 가능한 스킬이 있으면 그 자리에서 시전 (캐스트 틱은 이동 생략).
	const float dist = owner.DistanceTo(target->GetPosition());
	const float now = GetTimeManager().GetTotalTime();

	if (const auto choice = owner.PickCastable(now, dist))
	{
		phase_ = Phase::Casting;
		choice->tmpl->behavior->Execute(*choice->tmpl, owner, *target, now);
		owner.MarkSkillUsed(choice->skillId, now + choice->appliedCooldown);
		return;
	}

	// 4) 시전 불가. basic 사거리 기준으로 접근 / 대기.
	const float engageRange = owner.GetBasicSkillRange();
	if (engageRange <= 0.0f || dist > engageRange)
	{
		phase_ = Phase::Approach;
		owner.MoveToward(target->GetPosition(), deltaTime);
	}
	else
	{
		phase_ = Phase::Waiting;
		// 제자리 대기 — 쿨다운 회복을 기다린다.
	}
}


// ===========================================================================
// Return
// ===========================================================================

void ReturnState::OnEnter(Monster& owner)
{
	owner.ClearTarget();
	// Leash 초과로 Return 진입 시 aggro 도 함께 초기화 (신규 전투 시작으로 간주)
	owner.ClearAggro();
	owner.Heal(owner.GetMaxHp());

	// TODO: Buff 시스템을 도입한 후, 무적 버프 추가 필요.

	owner.GetZone().Broadcast(PacketMaker::MakeUnitHp(owner));
}

void ReturnState::OnUpdate(Monster& owner, const float deltaTime)
{
	float dist = owner.DistanceTo(owner.GetSpawnPos());
	if (dist <= 1.0f)
	{
		owner.GetFSM().ChangeState(MonsterStateId::Idle);
		return;
	}

	owner.MoveToward(owner.GetSpawnPos(), deltaTime);
}
```

- [ ] **Step 2: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: `Monster.cpp` 에서 `fsm_.AddState<ChaseState>(MonsterStateId::Chase)` 가 여전히 존재하므로 컴파일 에러 예상 — `ChaseState`, `AttackState` 미정의 + `MonsterStateId::Chase`/`Attack` enum 값 없음. 정상. Task 11 에서 해결.

이 Task 는 "변환의 중간 상태" 라 commit 은 Task 11 과 한 번에 묶어도 됨. 하지만 세분화 원칙에 따라 WIP 커밋으로 나눈다:

- [ ] **Step 3: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/MonsterStates.cpp
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): implement EngageState, remove Chase/Attack state impls [WIP]"
```

---

## Task 11: `Monster` 의 FSM 등록과 `DoAttack` 정리

**Files:**
- Modify: `mmosvr/GameServer/Monster.h`, `mmosvr/GameServer/Monster.cpp`

- [ ] **Step 1: `Monster.h` 에서 `DoAttack` 제거**

`Monster.h` 에서 다음 선언 삭제:

```cpp
// 지정한 스킬로 공격 실행. 스킬 선택은 호출자(AttackState) 책임.
void  DoAttack(const SkillTemplate& sk, Player& target);
```

`#include "AttackTypes.h"` 는 다른 곳에서 쓰이고 있으면 유지, 아니면 그대로 둔다. `SkillTemplate` 전방 선언도 유지.

- [ ] **Step 2: `Monster.cpp` 의 `InitAI` 에서 상태 등록 교체**

기존:

```cpp
fsm_.AddState<IdleState>(MonsterStateId::Idle);
fsm_.AddState<PatrolState>(MonsterStateId::Patrol);
fsm_.AddState<ChaseState>(MonsterStateId::Chase);
fsm_.AddState<AttackState>(MonsterStateId::Attack);
fsm_.AddState<ReturnState>(MonsterStateId::Return);
```

→ 교체:

```cpp
fsm_.AddState<IdleState>(MonsterStateId::Idle);
fsm_.AddState<PatrolState>(MonsterStateId::Patrol);
fsm_.AddState<EngageState>(MonsterStateId::Engage);
fsm_.AddState<ReturnState>(MonsterStateId::Return);
```

- [ ] **Step 3: `Monster.cpp` 의 `DoAttack` 함수 본체 삭제**

다음 블록을 완전히 삭제:

```cpp
void Monster::DoAttack(const SkillTemplate& sk, Player& target)
{
	// Monster 평타는 "기본 공격" 성격이라 Silence 는 면역 (LoL 관습). Stun 만 차단.
	if (!CanAttack()) return;
	SkillRuntime::Cast(sk, *this, target, GetZone());
}
```

해당 로직은 이미 Task 2 에서 `DefaultAttackBehavior::Execute` 로 이전되었다.

- [ ] **Step 4: 불필요해진 include 정리**

`Monster.cpp` 상단에서 `#include "SkillRuntime.h"` 가 `DoAttack` 에서만 쓰이고 있었는지 확인:

```bash
grep -n "SkillRuntime" /c/Users/qkrwlstn924/Desktop/dbi/mmosvr/GameServer/Monster.cpp
```

유일 사용처였다면 해당 include 삭제. 아니면 유지.

- [ ] **Step 5: 빌드 확인**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 성공. 엔진 전체 교체 완료.

- [ ] **Step 6: 서버 구동 + DummyClient 검증**

시나리오:
1. GameServer 구동 — 로그에 reference validation 에러 없는지
2. DummyClient 로 로그인 후 몬스터 리스폰 구역 접근
3. **관찰 기준 동작** (리팩토링 전과 동일해야 함):
   - 감지 거리 내 접근 시 몬스터가 플레이어 추격 (`S_MonsterState` 로 `state=2/Engage` 브로드캐스트)
   - basic 사거리 안에 들어가면 몬스터가 반복 공격 (`S_SkillHit`)
   - Goblin(tid=1001) 의 경우 special 폭탄(sid=1101)도 cast_range=8 내에서 5초 간격으로 발동 — basic 사거리(2.0) 밖에서도 발동되는 것 확인
   - leash 초과로 멀어지면 귀환 → HP 회복 → Idle

- [ ] **Step 7: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add mmosvr/GameServer/Monster.h mmosvr/GameServer/Monster.cpp
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "refactor(monster): wire EngageState in InitAI, remove DoAttack"
```

---

## Task 12: `game.proto` S_MonsterState 주석 업데이트

**Files:**
- Modify: `ShareDir/proto/game.proto`

- [ ] **Step 1: 주석만 수정 (와이어 포맷 불변)**

기존:

```proto
message S_MonsterState {
    int64 guid = 1;
    uint32 state = 2;           // 0=Idle, 1=Chase, 2=Attack, 3=Return
    int64 target_guid = 3;      // Chase/Attack target player GUID
}
```

→

```proto
message S_MonsterState {
    int64 guid = 1;
    uint32 state = 2;           // 0=Idle, 1=Patrol, 2=Engage, 3=Return (MonsterStateId enum)
    int64 target_guid = 3;      // Engage 중일 때 현재 target player GUID
}
```

- [ ] **Step 2: proto 재생성**

Run:
```bash
cd /c/Users/qkrwlstn924/Desktop/dbi/ShareDir && ./generate_proto.bat
```

Expected: 주석만 바뀐 변경이므로 `.pb.h/.pb.cc/.cs` 실질 내용 동일 (파일 타임스탬프만 변경될 수 있음).

- [ ] **Step 3: 빌드**

Run: `msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m`
Expected: 성공.

- [ ] **Step 4: Commit**

```bash
git -C /c/Users/qkrwlstn924/Desktop/dbi add ShareDir/proto/game.proto
# 생성된 proto 결과물도 포함 (스테이징이 필요한 것만)
git -C /c/Users/qkrwlstn924/Desktop/dbi status
# 변경된 .pb.h/.pb.cc/.cs 가 있으면 추가:
git -C /c/Users/qkrwlstn924/Desktop/dbi add -u
git -C /c/Users/qkrwlstn924/Desktop/dbi commit -m "proto(monster): update S_MonsterState.state comment for Engage"
```

---

## Task 13: 최종 검증 — 관찰 회귀 테스트

이번 Task 는 새 commit 없이 **검증만** 수행한다. 어디서든 회귀가 발견되면 해당 Task 로 돌아가 수정하고 추가 commit.

- [ ] **Step 1: Clean 빌드**

```bash
msbuild mmosvr.sln /p:Configuration=Debug /p:Platform=x64 /m /t:Rebuild
```
Expected: 경고는 허용되나 에러 0.

- [ ] **Step 2: `GameServer.exe` 구동 + DummyClient 체크리스트**

모든 항목이 리팩토링 이전 동작과 동일해야 한다.

1. **부팅**: "ResourceManager initialized" 로그, reference validation 에러 없음
2. **Aggro 감지**: Idle 몬스터 근처(≤ detectRange)로 Player 접근 → 몬스터가 Player 쪽으로 이동 시작 (`S_MonsterState.state = 2`)
3. **기본 사거리 내 지속 공격**: basic cast_range 안에 들어서면 cooldown 주기대로 `S_SkillHit` 이 쏟아짐
4. **Goblin 스페셜 (원거리 캐스트)**: tid=1001 Goblin 의 special sid=1101 (cast_range=8) 이 basic(cast_range=2) 밖에서도 5초 간격으로 발동하는지 — **이게 이번 리팩토링의 핵심 행동 보존 포인트**
5. **Leash 초과 귀환**: leash 초과까지 Player 가 몬스터를 끌면 몬스터가 `state = 3/Return` 으로 전환, HP 회복(`S_UnitHp`), spawn 도착 후 `state = 0/Idle`
6. **타겟 사망 시 귀환**: Player 가 사망하면 몬스터가 Return → Idle
7. **동시 교전**: 여러 Player 중 top aggro 대상 추격이 매 틱 재계산되는지 (aggro 조작 테스트)

- [ ] **Step 3: debug_tool 관찰 (선택)**

debug_tool 이 `S_MonsterState.state` 를 표시한다면 이제 `Chase/Attack` 대신 `Engage` 로만 표시됨. 이전보다 덜 세분화되지만 사용자가 수용한 trade-off (spec §4.3).

- [ ] **Step 4: 회귀 없으면 최종 커밋 없이 종료, 있으면 수정 커밋 추가**

---

## Self-Review Notes (작성자 자체 검토)

1. **Spec coverage:**
   - Spec §4.1 (상태 세트) → Task 9, 11
   - Spec §4.2 (EngageState 내부 동작) → Task 10
   - Spec §4.3 (phase 태그) → Task 9 (선언), Task 10 (세팅)
   - Spec §4.4 (ISkillBehavior) → Task 1, 2
   - Spec §4.5 (skill_templates.csv → behavior 바인딩) → Task 5, 6, 7
   - Spec §4.6 (PickCastable + CanCast) → Task 8
   - Spec §4.7 (MonsterGlobalState) → Task 10 의 GlobalState 블록
   - Spec §6 (삭제/이동 테이블) → Task 10 (Chase/Attack 제거), Task 11 (DoAttack 제거)
   - Spec §7 (테스트) → Task 11 step 6, Task 13

2. **Placeholder scan:** TBD/TODO/"add appropriate handling" 없음. "TODO: Buff 시스템 도입 후 무적 버프" 는 기존 코드에 원래 있던 주석 — 이번 범위 밖 의도적 보존.

3. **Type consistency:**
   - `ISkillBehavior::Execute(const SkillTemplate&, Monster&, Player&, float)` — Task 1 선언, Task 2 구현, Task 10 호출. 일치.
   - `SkillBehaviorRegistry::Register(std::string, Factory)` — Task 3 선언, Task 7 호출. 일치.
   - `Monster::PickCastable` 반환 타입 `SkillChoice { tmpl, skillId, appliedCooldown }` — 기존 코드 유지, Task 10 에서 `choice->tmpl->behavior->Execute(...)` 호출. 일치.
   - `MonsterStateId::Engage` — Task 9 (enum), Task 10 (case / ChangeState), Task 11 (AddState). 일치.

4. **Scope check:** 13개 task 가 한 세션에 이어 실행하기엔 길지만, 각 task 가 독립 commit 이라 중간 중단도 안전. subagent-driven-development 에 적합.
