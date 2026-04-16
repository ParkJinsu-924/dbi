#pragma once

#include "Utils/TSingleton.h"
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

	bool IsOnNavMesh(float x, float y, float z) const;

	bool FindNearestValidPosition(float x, float y, float z,
		float& outX, float& outY, float& outZ,
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
