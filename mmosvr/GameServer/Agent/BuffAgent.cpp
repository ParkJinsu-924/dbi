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

float BuffAgent::EffectiveMoveSpeed(const float baseSpeed) const
{
	float flat = 0.0f, pct = 0.0f;
	GetStatModifier(StatType::MoveSpeed, flat, pct);
	return (std::max)(0.0f, baseSpeed * (1.0f + pct) + flat);
}
