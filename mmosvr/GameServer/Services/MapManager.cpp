#include "pch.h"
#include "Services/MapManager.h"
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourCommon.h"

#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstring>

namespace
{
	constexpr uint32_t GSCN_MAGIC = 0x4E435347;
	constexpr uint32_t GSCN_VERSION = 1;

	// Recast build parameters (tuned for current map)
	constexpr float CELL_SIZE = 0.3f;
	constexpr float CELL_HEIGHT = 0.2f;
	constexpr float AGENT_HEIGHT = 2.0f;
	constexpr float AGENT_RADIUS = 0.5f;
	constexpr float AGENT_MAX_CLIMB = 0.5f;
	constexpr float AGENT_MAX_SLOPE = 45.0f;
	constexpr int REGION_MIN_SIZE = 8;
	constexpr int REGION_MERGE_SIZE = 20;
	constexpr float EDGE_MAX_LEN = 12.0f;
	constexpr float EDGE_MAX_ERROR = 1.3f;
	constexpr int VERTS_PER_POLY = 6;
	constexpr float DETAIL_SAMPLE_DIST = 6.0f;
	constexpr float DETAIL_SAMPLE_MAX_ERROR = 1.0f;

	// Tolerance for "on NavMesh" check
	constexpr float ON_MESH_TOLERANCE = 1.0f;

	std::string FindMapFile()
	{
		namespace fs = std::filesystem;

		// Try multiple candidate paths relative to executable
		std::vector<std::string> candidates = {
			"../../../../ShareDir/maps/default.scene.bin",
			"../../../ShareDir/maps/default.scene.bin",
			"../../ShareDir/maps/default.scene.bin",
			"../ShareDir/maps/default.scene.bin",
			"ShareDir/maps/default.scene.bin",
		};

		for (const auto& path : candidates)
		{
			if (fs::exists(path))
				return fs::absolute(path).string();
		}

		return {};
	}
}

void MapManager::LoadNavMesh()
{
	std::string mapPath = FindMapFile();
	if (mapPath.empty())
	{
		LOG_INFO("MapService: No map file found, movement validation disabled");
		return;
	}

	LOG_INFO("MapService: Loading " + mapPath);

	if (!LoadSceneGeometry(mapPath))
	{
		LOG_ERROR("MapService: Failed to load scene geometry");
		return;
	}

	LOG_INFO("MapService: Loaded " + std::to_string(vertexCount_) + " vertices, "
		+ std::to_string(triangleCount_) + " triangles");

	if (!BuildNavMesh())
	{
		LOG_ERROR("MapService: Failed to build NavMesh");
		return;
	}

	LOG_INFO("MapService: NavMesh built successfully");
}

bool MapManager::LoadSceneGeometry(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
		return false;

	// Read header
	uint32_t magic, version, vertCount, triCount, reserved;
	file.read(reinterpret_cast<char*>(&magic), 4);
	file.read(reinterpret_cast<char*>(&version), 4);
	file.read(reinterpret_cast<char*>(&vertCount), 4);
	file.read(reinterpret_cast<char*>(&triCount), 4);
	file.read(reinterpret_cast<char*>(&reserved), 4);

	if (magic != GSCN_MAGIC)
	{
		LOG_ERROR("MapService: Invalid magic: " + std::to_string(magic));
		return false;
	}
	if (version != GSCN_VERSION)
	{
		LOG_ERROR("MapService: Unsupported version: " + std::to_string(version));
		return false;
	}

	vertexCount_ = static_cast<int>(vertCount);
	triangleCount_ = static_cast<int>(triCount);

	// Read vertices (x, y, z floats)
	vertices_.resize(vertexCount_ * 3);
	file.read(reinterpret_cast<char*>(vertices_.data()),
		static_cast<std::streamsize>(vertexCount_) * 3 * sizeof(float));

	// Read triangles (i0, i1, i2 ints)
	triangles_.resize(triangleCount_ * 3);
	file.read(reinterpret_cast<char*>(triangles_.data()),
		static_cast<std::streamsize>(triangleCount_) * 3 * sizeof(int));

	return file.good() || file.eof();
}

bool MapManager::BuildNavMesh()
{
	// Calculate bounding box
	float bmin[3] = { vertices_[0], vertices_[1], vertices_[2] };
	float bmax[3] = { vertices_[0], vertices_[1], vertices_[2] };

	for (int i = 0; i < vertexCount_; ++i)
	{
		const float* v = &vertices_[i * 3];
		for (int j = 0; j < 3; ++j)
		{
			if (v[j] < bmin[j]) bmin[j] = v[j];
			if (v[j] > bmax[j]) bmax[j] = v[j];
		}
	}

	// Recast config
	rcConfig cfg{};
	cfg.cs = CELL_SIZE;
	cfg.ch = CELL_HEIGHT;
	cfg.walkableSlopeAngle = AGENT_MAX_SLOPE;
	cfg.walkableHeight = static_cast<int>(std::ceil(AGENT_HEIGHT / cfg.ch));
	cfg.walkableClimb = static_cast<int>(std::floor(AGENT_MAX_CLIMB / cfg.ch));
	cfg.walkableRadius = static_cast<int>(std::ceil(AGENT_RADIUS / cfg.cs));
	cfg.maxEdgeLen = static_cast<int>(EDGE_MAX_LEN / cfg.cs);
	cfg.maxSimplificationError = EDGE_MAX_ERROR;
	cfg.minRegionArea = REGION_MIN_SIZE * REGION_MIN_SIZE;
	cfg.mergeRegionArea = REGION_MERGE_SIZE * REGION_MERGE_SIZE;
	cfg.maxVertsPerPoly = VERTS_PER_POLY;
	cfg.detailSampleDist = DETAIL_SAMPLE_DIST < 0.9f ? 0 : cfg.cs * DETAIL_SAMPLE_DIST;
	cfg.detailSampleMaxError = cfg.ch * DETAIL_SAMPLE_MAX_ERROR;

	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);
	rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

	rcContext ctx;

	// Step 1: Create heightfield
	rcHeightfield* solid = rcAllocHeightfield();
	if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height,
		cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
	{
		LOG_ERROR("MapService: rcCreateHeightfield failed");
		rcFreeHeightField(solid);
		return false;
	}

	// Step 2: Rasterize triangles
	std::vector<unsigned char> triAreas(triangleCount_, 0);
	rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle,
		vertices_.data(), vertexCount_,
		triangles_.data(), triangleCount_,
		triAreas.data());

	if (!rcRasterizeTriangles(&ctx, vertices_.data(), vertexCount_,
		triangles_.data(), triAreas.data(), triangleCount_,
		*solid, cfg.walkableClimb))
	{
		LOG_ERROR("MapService: rcRasterizeTriangles failed");
		rcFreeHeightField(solid);
		return false;
	}

	// Step 3: Filter walkable areas
	rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
	rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
	rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

	// Step 4: Build compact heightfield
	rcCompactHeightfield* chf = rcAllocCompactHeightfield();
	if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb,
		*solid, *chf))
	{
		LOG_ERROR("MapService: rcBuildCompactHeightfield failed");
		rcFreeHeightField(solid);
		rcFreeCompactHeightfield(chf);
		return false;
	}
	rcFreeHeightField(solid);
	solid = nullptr;

	// Step 5: Erode walkable area
	if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
	{
		LOG_ERROR("MapService: rcErodeWalkableArea failed");
		rcFreeCompactHeightfield(chf);
		return false;
	}

	// Step 6: Build distance field and regions
	if (!rcBuildDistanceField(&ctx, *chf))
	{
		LOG_ERROR("MapService: rcBuildDistanceField failed");
		rcFreeCompactHeightfield(chf);
		return false;
	}

	if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
	{
		LOG_ERROR("MapService: rcBuildRegions failed");
		rcFreeCompactHeightfield(chf);
		return false;
	}

	// Step 7: Build contours
	rcContourSet* cset = rcAllocContourSet();
	if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
	{
		LOG_ERROR("MapService: rcBuildContours failed");
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		return false;
	}

	// Step 8: Build polygon mesh
	rcPolyMesh* pmesh = rcAllocPolyMesh();
	if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
	{
		LOG_ERROR("MapService: rcBuildPolyMesh failed");
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		rcFreePolyMesh(pmesh);
		return false;
	}

	// Step 9: Build detail mesh
	rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
	if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf,
		cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
	{
		LOG_ERROR("MapService: rcBuildPolyMeshDetail failed");
		rcFreeCompactHeightfield(chf);
		rcFreeContourSet(cset);
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return false;
	}

	rcFreeCompactHeightfield(chf);
	rcFreeContourSet(cset);

	// Step 10: Create Detour nav mesh data
	for (int i = 0; i < pmesh->npolys; ++i)
	{
		if (pmesh->areas[i] == RC_WALKABLE_AREA)
			pmesh->flags[i] = 1; // walkable
	}

	dtNavMeshCreateParams params{};
	params.verts = pmesh->verts;
	params.vertCount = pmesh->nverts;
	params.polys = pmesh->polys;
	params.polyAreas = pmesh->areas;
	params.polyFlags = pmesh->flags;
	params.polyCount = pmesh->npolys;
	params.nvp = pmesh->nvp;
	params.detailMeshes = dmesh->meshes;
	params.detailVerts = dmesh->verts;
	params.detailVertsCount = dmesh->nverts;
	params.detailTris = dmesh->tris;
	params.detailTriCount = dmesh->ntris;
	params.walkableHeight = AGENT_HEIGHT;
	params.walkableRadius = AGENT_RADIUS;
	params.walkableClimb = AGENT_MAX_CLIMB;
	rcVcopy(params.bmin, pmesh->bmin);
	rcVcopy(params.bmax, pmesh->bmax);
	params.cs = cfg.cs;
	params.ch = cfg.ch;
	params.buildBvTree = true;

	unsigned char* navData = nullptr;
	int navDataSize = 0;
	if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
	{
		LOG_ERROR("MapService: dtCreateNavMeshData failed");
		rcFreePolyMesh(pmesh);
		rcFreePolyMeshDetail(dmesh);
		return false;
	}

	rcFreePolyMesh(pmesh);
	rcFreePolyMeshDetail(dmesh);

	// Step 11: Init Detour NavMesh
	navMesh_ = dtAllocNavMesh();
	if (!navMesh_)
	{
		dtFree(navData);
		LOG_ERROR("MapService: dtAllocNavMesh failed");
		return false;
	}

	dtStatus status = navMesh_->init(navData, navDataSize, DT_TILE_FREE_DATA);
	if (dtStatusFailed(status))
	{
		dtFree(navData);
		dtFreeNavMesh(navMesh_);
		navMesh_ = nullptr;
		LOG_ERROR("MapService: dtNavMesh::init failed");
		return false;
	}

	// Step 12: Init NavMesh query
	navQuery_ = dtAllocNavMeshQuery();
	if (!navQuery_)
	{
		dtFreeNavMesh(navMesh_);
		navMesh_ = nullptr;
		LOG_ERROR("MapService: dtAllocNavMeshQuery failed");
		return false;
	}

	status = navQuery_->init(navMesh_, 2048);
	if (dtStatusFailed(status))
	{
		dtFreeNavMeshQuery(navQuery_);
		navQuery_ = nullptr;
		dtFreeNavMesh(navMesh_);
		navMesh_ = nullptr;
		LOG_ERROR("MapService: dtNavMeshQuery::init failed");
		return false;
	}

	LOG_INFO("MapService: NavMesh ready (" + std::to_string(params.polyCount) + " polys)");
	return true;
}

bool MapManager::IsOnNavMesh(float x, float y, float z) const
{
	if (!navQuery_)
		return true; // No map loaded, allow all movement

	float pos[3] = { x, y, z };
	float halfExtents[3] = { ON_MESH_TOLERANCE, ON_MESH_TOLERANCE, ON_MESH_TOLERANCE };
	float nearestPt[3];

	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);

	dtPolyRef nearestRef = 0;
	dtStatus status = navQuery_->findNearestPoly(pos, halfExtents, &filter,
		&nearestRef, nearestPt);

	if (dtStatusFailed(status) || nearestRef == 0)
		return false;

	// Check horizontal distance to nearest point
	float dx = nearestPt[0] - x;
	float dz = nearestPt[2] - z;
	float distSq = dx * dx + dz * dz;

	return distSq < (ON_MESH_TOLERANCE * ON_MESH_TOLERANCE);
}

bool MapManager::FindNearestValidPosition(float x, float y, float z,
	float& outX, float& outY, float& outZ,
	float searchRadius) const
{
	if (!navQuery_)
		return false;

	float pos[3] = { x, y, z };
	float halfExtents[3] = { searchRadius, searchRadius, searchRadius };
	float nearestPt[3];

	dtQueryFilter filter;
	filter.setIncludeFlags(0xFFFF);
	filter.setExcludeFlags(0);

	dtPolyRef nearestRef = 0;
	dtStatus status = navQuery_->findNearestPoly(pos, halfExtents, &filter,
		&nearestRef, nearestPt);

	if (dtStatusFailed(status) || nearestRef == 0)
		return false;

	outX = nearestPt[0];
	outY = nearestPt[1];
	outZ = nearestPt[2];
	return true;
}
