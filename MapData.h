#pragma once

#include <cstdint>

namespace MapData {

struct Platform {
    double x;
    double y;
    double width;
    double height;
    double rotation;
};

struct MapGeometry {
    int mapId;
    double camX;
    double camY;
    double camW;
    double camH;
    Platform platforms[256];
    int platformCount;
};

// Get map geometry by ID (returns nullptr if not found)
const MapGeometry* GetMapGeometry(int mapId);

// Get map display name by ID (returns nullptr if not found)
const char* GetMapName(int mapId);

// Get map level name by ID (returns nullptr if not found)
const char* GetMapLevelName(int mapId);

} // namespace MapData
