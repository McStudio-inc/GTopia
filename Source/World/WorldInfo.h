#pragma once

#include "../Math/Vector2.h"
#include "../Memory/MemoryBuffer.h"
#include "../Precompiled.h"
#include "WorldObjectManager.h"
#include "WorldTileManager.h"

#define DEFAULT_WORLD_VERSION_FOR_DB 14

static const std::vector<std::pair<float, uint16>> sWorldVersionMap = {
    {4.11f, 23}, {3.68f, 20}, {3.02f, 14}, {2.995f, 13}, {2.88f, 4}, {1.30f, 3}, {0.0f, 0}};

enum eWorldGenerationType
{
    WORLD_GENERATION_DEFAULT = 0,
    WORLD_GENERATION_CLEAR = 1
};

enum eWeatherTypes
{
    WEATHER_TYPE_DEFAULT = 0,
    WEATHER_TYPE_SUNSET = 1,
    WEATHER_TYPE_NIGHT = 2,
    WEATHER_TYPE_DESERT = 3,
    WEATHER_TYPE_SUNNY = 4,
    WEATHER_TYPE_RAINY_CITY = 5,
    WEATHER_TYPE_HARVEST = 6
};

bool IsValidWorldName(const string& worldName, bool allowColon = false);
uint16 ChooseWorldVersionForClient(float gameVersion);

class WorldInfo
{
public:
    WorldInfo();
    virtual ~WorldInfo();

public:
    virtual void OnHeartMonitorAdded(TileInfo* pTile);
    virtual void OnHeartMonitorRemoved(TileInfo* pTile);
    virtual void OnSuckerBlockAdded(TileInfo* pTile);
    virtual void OnSuckerBlockRemoved(TileInfo* pTile);

    virtual void GenerateWorld(eWorldGenerationType type);

public:
    void Kill();

    bool Serialize(MemoryBuffer& memBuffer, bool write, bool database, int16 worldVersion = -1,
                   float gameVersion = 0.0f);
    uint32 GetMemEstimate(bool database, int16 worldVersion = -1, float gameVersion = 0.0f);

    void SetName(const string& worldName) { m_name = worldName; }
    const string& GetWorlName() const { return m_name; }

    void SetCurrentWeather(uint32 newWeather) { m_currentWeather = newWeather; }
    uint32 GetCurrentWeather() const { return m_currentWeather; }

    uint32 GetDefaultWeather() const { return m_defaultWeather; }

    uint16 GetWorldVersion() const { return m_version; }

    WorldTileManager* GetTileManager() { return m_pTileMgr; };
    WorldObjectManager* GetObjectManager() { return m_pObjMgr; }

private:
    uint16 m_version;
    uint32 m_flags;
    string m_name;

    WorldTileManager* m_pTileMgr;
    WorldObjectManager* m_pObjMgr;

    uint16 m_defaultWeather;
    uint16 m_currentWeather;
};