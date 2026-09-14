#include "WorldInfo.h"
#include "../Math/Random.h"
#include "../Utils/StringUtils.h"

bool IsValidWorldName(const string& worldName, bool allowColon)
{
    const char* src = worldName.c_str();

    while (*src)
    {
        if ((IsAlpha(*src) && IsUpper(*src)) || IsDigit(*src) || (allowColon && *src == ':'))
        {
            src++;
            continue;
        }

        return false;
    }

    return true;
}

uint16 ChooseWorldVersionForClient(float gameVersion)
{
    for (auto& [minVersion, worldVer] : sWorldVersionMap)
    {
        if (gameVersion >= minVersion)
        {
            return worldVer;
        }
    }

    return 0;
}

WorldInfo::WorldInfo() : m_version(DEFAULT_WORLD_VERSION_FOR_DB), m_flags(0), m_defaultWeather(0), m_currentWeather(0)
{
    m_pTileMgr = new WorldTileManager(this);
    m_pObjMgr = new WorldObjectManager();
}

WorldInfo::~WorldInfo()
{
    Kill();
}

void WorldInfo::OnHeartMonitorAdded(TileInfo* pTile) {}
void WorldInfo::OnHeartMonitorRemoved(TileInfo* pTile) {}

void WorldInfo::OnSuckerBlockAdded(TileInfo* pTile) {}
void WorldInfo::OnSuckerBlockRemoved(TileInfo* pTile) {}

void WorldInfo::Kill()
{
    SAFE_DELETE(m_pTileMgr);
    SAFE_DELETE(m_pObjMgr);
}

// todo world version for importing rgt
// database = false, write = false

bool WorldInfo::Serialize(MemoryBuffer& memBuffer, bool write, bool database, int16 worldVersion, float gameVersion)
{
    if (database && worldVersion == -1)
        worldVersion = m_version;

    memBuffer.ReadWrite(worldVersion, write);
    memBuffer.ReadWrite(m_flags, write);
    memBuffer.ReadWriteString(m_name, write);

    if (!m_pTileMgr->Serialize(memBuffer, write, database, this, worldVersion, gameVersion))
        return false;

    if (!database && write && gameVersion >= 5.40f) // 5.40 is not the actual value lazy to dig for it
    {
        uint32 unk = 0;
        memBuffer.ReadWrite(unk, write);
        memBuffer.ReadWrite(unk, write);
        memBuffer.ReadWrite(unk, write);
    }

    m_pObjMgr->Serialize(memBuffer, write, database);

    uint16 unused = 0;
    memBuffer.ReadWrite(m_defaultWeather, write);
    memBuffer.ReadWrite(unused, write);
    memBuffer.ReadWrite(m_currentWeather, write);
    memBuffer.ReadWrite(unused, write);

    uint32 unused2 = 0;
    memBuffer.ReadWrite(unused2, write);
    return true;
}

void WorldInfo::GenerateWorld(eWorldGenerationType type)
{
    switch (type)
    {
        case WORLD_GENERATION_DEFAULT:
        {
            m_defaultWeather = WEATHER_TYPE_DEFAULT;
            m_currentWeather = WEATHER_TYPE_DEFAULT;

            m_pTileMgr->GenerateDefaultMap();
            break;
        }

        case WORLD_GENERATION_CLEAR:
        {
            m_defaultWeather = WEATHER_TYPE_DEFAULT;
            m_currentWeather = WEATHER_TYPE_DEFAULT;

            m_pTileMgr->GenerateClearMap();
            break;
        }
    }
}

uint32 WorldInfo::GetMemEstimate(bool database, int16 worldVersion, float gameVersion)
{
    if (database && worldVersion == -1)
        worldVersion = m_version;

    uint32 memSize = 0;
    memSize += sizeof(uint16) + sizeof(m_flags) + 2 + m_name.size();
    memSize += m_pTileMgr->GetMemEstimate(database, this, worldVersion, gameVersion);
    memSize += m_pObjMgr->GetMemEstimate();

    if (worldVersion > 2)
    {
        memSize += sizeof(m_defaultWeather) + sizeof(m_currentWeather) + sizeof(uint16) * 2 + sizeof(uint32);
    }

    if (!database && gameVersion > 5.40f)
    {
        memSize += sizeof(uint32) * 3;
    }

    return memSize;
}