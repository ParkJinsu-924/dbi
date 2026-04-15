#pragma once

#include "GameService.h"
#include "Utils/TSingleton.h"
#include <vector>
#include <string>
#include <cstdint>

class dtNavMesh;
class dtNavMeshQuery;

#define GetMapService() MapService::Instance()

class MapService : public TSingleton<MapService>, public GameService
{
public:
	void Init() override;
	void Update(float deltaTime) override;
	void Shutdown() override;

	bool IsLoaded() const { return navQuery_ != nullptr; }

	// Check if a world position is on the NavMesh
	bool IsOnNavMesh(float x, float y, float z) const;

	// Find the nearest valid position on the NavMesh
	bool FindNearestValidPosition(float x, float y, float z,
		float& outX, float& outY, float& outZ,
		float searchRadius = 3.0f) const;

private:
	bool LoadSceneGeometry(const std::string& path);
	bool BuildNavMesh();

	// Loaded geometry
	std::vector<float> vertices_;    // x,y,z interleaved
	std::vector<int> triangles_;     // 3 indices per triangle
	int vertexCount_ = 0;
	int triangleCount_ = 0;

	// Recast/Detour
	dtNavMesh* navMesh_ = nullptr;
	dtNavMeshQuery* navQuery_ = nullptr;
};
