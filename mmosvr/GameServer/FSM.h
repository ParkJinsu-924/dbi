#pragma once

#include <unordered_map>
#include <memory>
#include <type_traits>
#include <functional>


// ===========================================================================
// IState — 상태 인터페이스
// ===========================================================================
// TOwner: 이 상태 머신을 소유하는 엔티티 타입 (Monster, Npc, Boss 등)

template<typename TOwner>
class IState
{
public:
	virtual ~IState() = default;

	virtual void OnEnter(TOwner& /*owner*/) {}
	virtual void OnUpdate(TOwner& owner, float deltaTime) = 0;
	virtual void OnExit(TOwner& /*owner*/) {}
};


// ===========================================================================
// StateMachine — 유한 상태 머신
// ===========================================================================
// TOwner:   엔티티 타입
// TStateId: 상태 식별자 (enum class 권장)

template<typename TOwner, typename TStateId>
	requires std::is_enum_v<TStateId>
class StateMachine
{
public:
	// --- 타입 ---
	using StatePtr       = std::unique_ptr<IState<TOwner>>;
	using ChangeCallback = std::function<void(TStateId /*prev*/, TStateId /*next*/)>;

	StateMachine() = default;
	~StateMachine() = default;

	StateMachine(const StateMachine&) = delete;
	StateMachine& operator=(const StateMachine&) = delete;
	StateMachine(StateMachine&&) = default;
	StateMachine& operator=(StateMachine&&) = default;

	// ----- 상태 등록 -----

	// 개별 상태를 in-place 생성하여 등록
	template<typename TState, typename... Args>
	TState* AddState(TStateId id, Args&&... args)
	{
		auto state = std::make_unique<TState>(std::forward<Args>(args)...);
		TState* raw = state.get();
		states_[id] = std::move(state);
		return raw;
	}

	// GlobalState: 매 틱 현재 상태 Update 이전에 항상 실행
	// (사망 체크, 버프 만료, CC기 등 모든 상태에 공통인 로직)
	template<typename TState, typename... Args>
	TState* SetGlobalState(Args&&... args)
	{
		globalState_ = std::make_unique<TState>(std::forward<Args>(args)...);
		return static_cast<TState*>(globalState_.get());
	}

	// 상태 전환 시 콜백 (로그, 패킷 브로드캐스트 등)
	void SetOnStateChanged(ChangeCallback cb) { onStateChanged_ = std::move(cb); }

	// ----- 라이프사이클 -----

	// 초기 상태 설정. 모든 AddState 호출 후 한 번만 호출.
	void Start(TOwner& owner, TStateId initialState)
	{
		currentId_ = initialState;
		current_ = FindState(currentId_);

		if (globalState_)
			globalState_->OnEnter(owner);
		if (current_)
			current_->OnEnter(owner);

		started_ = true;
	}

	// 매 틱 호출
	void Update(TOwner& owner, float deltaTime)
	{
		if (!started_)
			return;

		if (globalState_)
			globalState_->OnUpdate(owner, deltaTime);

		if (current_)
			current_->OnUpdate(owner, deltaTime);
	}

	// 상태 전환. Exit(old) -> Enter(new) 순서 보장.
	void ChangeState(TOwner& owner, TStateId newState)
	{
		if (newState == currentId_ && current_)
			return;

		TStateId prevId = currentId_;

		if (current_)
			current_->OnExit(owner);

		currentId_ = newState;
		current_ = FindState(currentId_);

		if (current_)
			current_->OnEnter(owner);

		if (onStateChanged_)
			onStateChanged_(prevId, currentId_);
	}

	// ----- 조회 -----

	TStateId GetCurrentStateId() const { return currentId_; }
	bool     IsStarted()         const { return started_; }

	// 특정 상태 인스턴스에 접근 (설정용)
	template<typename TState>
	TState* GetStateAs(TStateId id) const
	{
		auto* base = FindState(id);
		return base ? dynamic_cast<TState*>(base) : nullptr;
	}

private:
	IState<TOwner>* FindState(TStateId id) const
	{
		auto it = states_.find(id);
		return (it != states_.end()) ? it->second.get() : nullptr;
	}

	// enum을 키로 쓰기 위한 해시
	struct EnumHash
	{
		std::size_t operator()(TStateId v) const noexcept
		{
			return std::hash<std::underlying_type_t<TStateId>>{}(
				static_cast<std::underlying_type_t<TStateId>>(v));
		}
	};

	// states_: all registered states, keyed by TStateId.
	//          Owns the state objects via unique_ptr.
	std::unordered_map<TStateId, StatePtr, EnumHash> states_;

	// globalState_: optional state that runs every tick BEFORE the current state.
	//              Use for logic common to all states (e.g. death check, CC, buff expiry).
	//              Update order: globalState_->OnUpdate() -> current_->OnUpdate()
	StatePtr  globalState_;

	// currentId_: the TStateId of the currently active state.
	TStateId  currentId_{};

	// current_: raw pointer to the active state in states_ map for fast access.
	//           Updated on ChangeState(). Lifecycle: OnExit(old) -> OnEnter(new).
	IState<TOwner>* current_ = nullptr;

	bool started_ = false;
	ChangeCallback onStateChanged_;
};
