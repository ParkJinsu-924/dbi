#pragma once

#include "Utils/TSingleton.h"
#include "common.pb.h"
#include <vector>
#include <string>
#include <cstdint>

class dtNavMesh;
class dtNavMeshQuery;

#define GetMapManager() MapManager::Instance()

class MapManager : public TSingleton<MapManager>
{
public:
	void LoadNavMesh();

	bool IsLoaded() const { return navQuery_ != nullptr; }

	// 2D 좌표 기반 public API — Recast/Detour 는 내부에서 3D(y=0)로 채워 호출.
	// 세계 전체가 평면이므로 수직 정보는 외부에 노출하지 않는다.
	bool IsOnNavMesh(const Proto::Vector2& pos) const;

	// valid pos 를 out 에 기록. searchRadius 내에 없으면 false.
	bool FindNearestValidPosition(const Proto::Vector2& pos,
		Proto::Vector2& outPos,
		float searchRadius = 3.0f) const;

private:
	bool LoadSceneGeometry(const std::string& path);
	bool BuildNavMesh();

	std::vector<float> vertices_;
	std::vector<int> triangles_;
	int vertexCount_ = 0;
	int triangleCount_ = 0;

	dtNavMesh* navMesh_ = nullptr;
	dtNavMeshQuery* navQuery_ = nullptr;
};
