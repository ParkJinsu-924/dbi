#include "pch.h"
#include "BuffContainer.h"
#include "Effect.h"
#include "Unit.h"
#include "Zone.h"
#include "PacketMaker.h"


bool BuffContainer::Add(const Effect& e, const long long casterGuid)
{
	if (e.duration <= 0.0f)
		return false;

	// 같은 eid 가 이미 부착돼 있으면 refresh (Phase 1 정책).
	for (auto& entry : entries_)
	{
		if (entry.effect->eid == e.eid)
		{
			entry.remainingDuration = e.duration;
			entry.casterGuid        = casterGuid;
			if (const auto zone = owner_->GetZone())
				zone->Broadcast(PacketMaker::MakeBuffApplied(owner_->GetGuid(), e, casterGuid));
			return true;
		}
	}

	entries_.push_back({ &e, casterGuid, e.duration });
	if (const auto zone = owner_->GetZone())
		zone->Broadcast(PacketMaker::MakeBuffApplied(owner_->GetGuid(), e, casterGuid));
	return true;
}

void BuffContainer::Tick(const float dt)
{
	if (entries_.empty())
		return;

	Zone* zone = owner_->GetZone();

	// 만료된 엔트리 제거 (swap-and-pop 대신 erase-remove 로 순서 유지 — 로그 가독성).
	for (auto it = entries_.begin(); it != entries_.end();)
	{
		it->remainingDuration -= dt;
		if (it->remainingDuration <= 0.0f)
		{
			const int32 expiredEid = it->effect->eid;
			it = entries_.erase(it);
			if (zone)
				zone->Broadcast(PacketMaker::MakeBuffRemoved(owner_->GetGuid(), expiredEid));
		}
		else
		{
			++it;
		}
	}
}

bool BuffContainer::Remove(const int32 eid)
{
	for (auto it = entries_.begin(); it != entries_.end(); ++it)
	{
		if (it->effect->eid == eid)
		{
			entries_.erase(it);
			if (const auto zone = owner_->GetZone())
				zone->Broadcast(PacketMaker::MakeBuffRemoved(owner_->GetGuid(), eid));
			return true;
		}
	}
	return false;
}

uint32 BuffContainer::GetCCFlags() const
{
	uint32 flags = 0;
	for (const auto& e : entries_)
		flags |= static_cast<uint32>(e.effect->cc_flag);
	return flags;
}

void BuffContainer::GetStatModifier(const StatType stat, float& outFlat, float& outPercent) const
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
