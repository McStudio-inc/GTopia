#include "Context.h"
#include "IO/Log.h"
#include "Math/Math.h"
#include "Math/Random.h"
#include "Player/GamePlayer.h"
#include "Player/RoleManager.h"
#include "Server/AdminServer.h"
#include "Server/GameServer.h"
#include "Server/ServerManager.h"
#include "Utils/Timer.h"
#include "World/WorldManager.h"

void DatabaseThreadFunc()
{
    uint64 lastLogWriteTime = Time::GetSystemTime();
    uint64 nextTick = Time::GetSystemTime();

    Context* pContext = GetContext();
    DatabaseWorker* pWorker = GetContext()->GetDatabasePool()->GetWorker(0);

    while (GetContext()->IsRunning())
    {
        if (pWorker)
        {
            pWorker->Update();
            CRASH_SET("DatabaseConnected", pWorker->IsConnected());
        }
        else
        {
            CRASH_SET("DatabaseConnected", false);
        }

        uint64 logWriteStart = Time::GetSystemTime();
        if (logWriteStart - lastLogWriteTime >= 5000)
        {
            GetLog()->Write();
            lastLogWriteTime = Time::GetSystemTime();
        }
    }
}

void EventThreadFunc()
{
    GameServer* pGameServer = GetGameServer();
    ServerManager* pServerMgr = GetServerManager();
    AdminServer* pAdminServer = GetAdminServer();
    Context* pContext = GetContext();

    uint64 lastCalculateTime = Time::GetSystemTime();
    uint64 totalWorkTime = 0;

    while (pContext->IsRunning())
    {
        uint64 currentTime = Time::GetSystemTime();

        uint32 elapsedMs = (uint32)(currentTime - lastCalculateTime);
        if (elapsedMs >= 1000)
        {
            uint32 permille = (uint32)((totalWorkTime * 1000) / elapsedMs);

            if (permille > 1000)
            {
                permille = 1000;
            }

            pContext->GetRuntimeStats().GetPerfStats().netCpuPermille = permille;

            totalWorkTime = 0;
            lastCalculateTime = currentTime;
        }

        uint64 workStart = Time::GetSystemTime();

        pGameServer->Update();
        pServerMgr->Update(false);
        pAdminServer->Update(); // todo here handle on gameloop

        uint64 workEnd = Time::GetSystemTime();
        totalWorkTime += (workEnd - workStart);

        SleepMS(1);
    }
}

void ProcessDatabaseResults(uint64 maxTimeMS)
{
    DatabasePool* pDatabasePool = GetContext()->GetDatabasePool();
    if (!pDatabasePool)
    {
        return;
    }

    QueryTaskResult taskRes;
    Timer startTime;

    while (pDatabasePool->GetResult(taskRes))
    {
        if (taskRes.callback)
        {
            taskRes.callback(std::move(taskRes));
            taskRes.Destroy();
        }

        if (startTime.GetElapsedTime() >= maxTimeMS)
        {
            break;
        }
    }
}

void RunGameLoop()
{
    Context* pContext = GetContext();
    GameServer* pGameServer = GetGameServer();
    ServerManager* pServerMgr = GetServerManager();

    uint64 now = Time::GetSystemTime();
    uint64 nextTick = now + GAME_TICK_MS;

    uint64 lastPerfUpdateTime = now;

    uint64 tickDurSum = 0;
    uint32 tickCount = 0;

    uint32 intervalMaxTickMs = 0;
    uint32 intervalMaxLagSpikeMs = 0;

    uint64 totalWorkTimeInInterval = 0;
    uint64 loopIterStart = now;

    while (pContext->IsRunning())
    {
        loopIterStart = Time::GetSystemTime();
        now = loopIterStart;
        uint32 loops = 0;

        pServerMgr->UpdateServers();
        pServerMgr->UpdateTCPLogic(NETWORK_BUDGET_MS);
        ProcessDatabaseResults(DB_RESULT_BUDGET_MS);

        if (pContext->IsShutting())
        {
            pContext->Stop();
            continue;
        }

        while (now >= nextTick && loops < MAX_CATCHUP_TICKS)
        {
            uint64 tickStart = Time::GetSystemTime();

            pGameServer->UpdateGameLogic(GAME_TICK_MS);

            uint64 tickEnd = Time::GetSystemTime();
            uint32 tickDur = (uint32)(tickEnd - tickStart);

            tickDurSum += tickDur;
            ++tickCount;

            intervalMaxTickMs = Max(intervalMaxTickMs, tickDur);
            if (tickDur > GAME_TICK_MS)
            {
                uint32 currentSpike = tickDur - GAME_TICK_MS;
                intervalMaxLagSpikeMs = Max(intervalMaxLagSpikeMs, currentSpike);
            }

            nextTick += GAME_TICK_MS;
            ++loops;
            now = Time::GetSystemTime();
        }

        if (now >= nextTick)
        {
            nextTick = now + GAME_TICK_MS;
        }

        uint64 loopIterEnd = Time::GetSystemTime();
        totalWorkTimeInInterval += (loopIterEnd - loopIterStart);

        if (now - lastPerfUpdateTime >= PERF_SAMPLE_INTERVAL_MS)
        {
            ContextPerfStats& perf = pContext->GetRuntimeStats().GetPerfStats();
            uint32 elapsedIntervalMs = (uint32)(now - lastPerfUpdateTime);

            if (tickCount > 0)
            {
                perf.avgTickMs = (uint32)(tickDurSum / tickCount);
            }
            else
            {
                perf.avgTickMs = 0;
            }

            perf.maxTickMs = intervalMaxTickMs;
            perf.lagSpikeMs = intervalMaxLagSpikeMs;

            perf.cpuPermille = (uint32)((totalWorkTimeInInterval * 1000) / elapsedIntervalMs);
            if (perf.cpuPermille > 1000)
            {
                perf.cpuPermille = 1000;
            }

            tickDurSum = 0;
            tickCount = 0;
            intervalMaxTickMs = 0;
            intervalMaxLagSpikeMs = 0;
            totalWorkTimeInInterval = 0;
            lastPerfUpdateTime = now;
        }

        if (nextTick > now)
        {
            SleepMS((uint32)(nextTick - now));
        }
    }
}

/*void RegisterBalancedWorlds()
{
    GameConfig* pGameConfig = GetContext()->GetGameConfig();
    WorldManager* pWorldMgr = GetWorldManager();

    pWorldMgr->SetBalancerEnabled(pGameConfig->isWorldBalancerEnabled);
    if(!pWorldMgr->IsBalancerEnabled())
        return;

    for(auto& balance : pGameConfig->balancedWorlds)
    {
        pWorldMgr->RegisterBalancedWorld(balance);
    }
}*/

int main(int argc, char const* argv[])
{
    SystemSignal::RegisterShutdownHook(GetContext()->GetShutdownFlag());

    if (!GetLog()->InitFile(GetProgramPath() + "/logs/log_MASTER.txt"))
    {
        LOGGER_LOG_ERROR_ASAP("Failed to init log file, maybe try to create 'logs' folder?");
        return 0;
    }

    LOGGER_LOG_INFO_ASAP("Starting Master Server");
    LOGGER_LOG_INFO_ASAP("Project created by keichira https://github.com/keichira/GTopia")

    GetContext()->Init();
    SetRandomSeed(Time::GetSystemTime());
    RandomizeRandomSeed();

    GetContext()->SetID(0);

    auto pGameConfig = GetContext()->GetGameConfig();
    if (pGameConfig->LoadServersMaster(GetProgramPath() + "/servers.txt") == 0)
    {
        LOGGER_LOG_ERROR_ASAP("Failed to load servers.txt");
        GetContext()->Kill();
        return 0;
    }
    LOGGER_LOG_INFO_ASAP("Loaded %d servers from servers.txt", pGameConfig->servers.size());

    if (!pGameConfig->LoadConfig(GetProgramPath() + "/config.txt"))
    {
        LOGGER_LOG_ERROR_ASAP("Failed to load config.txt");
        GetContext()->Kill();
        return 0;
    }

    if (!GetRoleManager()->Load(GetProgramPath() + "/roles.txt"))
    {
        LOGGER_LOG_ERROR_ASAP("Failed to load roles.txt");
        GetContext()->Kill();
        return 0;
    }

    auto masterServerInfo = pGameConfig->servers[0];
    if (!GetServerManager()->Init(masterServerInfo.lanIP, masterServerInfo.tcpPort))
    {
        LOGGER_LOG_ERROR_ASAP("Failed to initialize netsocket on %s:%d", masterServerInfo.lanIP.c_str(),
                              masterServerInfo.tcpPort);
        GetContext()->Kill();
        return 0;
    }
    LOGGER_LOG_INFO_ASAP("Started netsocket on %s:%d", masterServerInfo.lanIP.c_str(), masterServerInfo.tcpPort);

    DatabaseConnectConfig dbConfig;
    dbConfig.host = pGameConfig->database.host.c_str();
    dbConfig.user = pGameConfig->database.user.c_str();
    dbConfig.pass = pGameConfig->database.pass.c_str();
    dbConfig.database = pGameConfig->database.database.c_str();
    dbConfig.port = pGameConfig->database.port;

    if (!GetContext()->GetDatabasePool()->Init(1, dbConfig))
    {
        LOGGER_LOG_ERROR_ASAP(
            "Failed to initialize database pool, database credentials might be wrong check config.txt");
        GetContext()->Kill();
        return 0;
    }
    LOGGER_LOG_INFO_ASAP("Loaded %d workers for database", GetContext()->GetDatabasePool()->GetWorkerSize());

    if (!GetGameServer()->Init(masterServerInfo.wanIP, masterServerInfo.udpPort))
    {
        LOGGER_LOG_ERROR_ASAP("Failed to initialize game server on %s:%d", masterServerInfo.wanIP.c_str(),
                              masterServerInfo.udpPort);
        GetContext()->Kill();
        return 0;
    }
    GetGameServer()->SetENetIncomeCmdType(pGameConfig->enetIncomeCmdType);
    LOGGER_LOG_INFO_ASAP("Started game server on %s:%d", masterServerInfo.wanIP.c_str(), masterServerInfo.udpPort);

    // RegisterBalancedWorlds();

    AdminServer* pAdminServer = GetAdminServer();
    if (pAdminServer->LoadConfigFromFile(GetProgramPath() + "/admin_server_config.txt"))
    {
        if (!pAdminServer->IsEnabled())
        {
            LOGGER_LOG_INFO_ASAP("Not starting admin server its disabled");
        }
        else
        {
            if (!pAdminServer->Init())
            {
                LOGGER_LOG_ERROR_ASAP("Failed to initialize admin server on %s:%d", pAdminServer->GetHost().c_str(),
                                      pAdminServer->GetPort());
                GetContext()->Kill();
                return 0;
            }
            else
            {
                LOGGER_LOG_INFO_ASAP("Started admin server on %s:%d", pAdminServer->GetHost().c_str(),
                                     pAdminServer->GetPort());
            }
        }
    }
    else
    {
        LOGGER_LOG_ERROR_ASAP("Failed to load admin_server_config.txt not gonna initialize admin server");
    }

    gNetBurstConfig.threshold.heavyQueueSize = pGameConfig->netThreshold.heavyQueueSize;
    gNetBurstConfig.threshold.panicQueueSize = pGameConfig->netThreshold.panicQueueSize;
    gNetBurstConfig.threshold.heavyCpuPermille = pGameConfig->netThreshold.heavyCpuPermille;
    gNetBurstConfig.threshold.panicCpuPermille = pGameConfig->netThreshold.panicBurst;
    gNetBurstConfig.normalBurst = pGameConfig->netThreshold.normalBurst;
    gNetBurstConfig.heavyBurst = pGameConfig->netThreshold.heavyBurst;
    gNetBurstConfig.panicBurst = pGameConfig->netThreshold.panicBurst;

    std::thread dbThread(DatabaseThreadFunc);
    std::thread eventThread(EventThreadFunc);

    RunGameLoop();

    LOGGER_LOG_INFO_ASAP("Killing Master server");

    GetLog()->Flush();
    GetContext()->GetDatabasePool()->GetWorker(0)->SendFakeTask();

    if (dbThread.joinable())
        dbThread.join();

    if (eventThread.joinable())
        eventThread.join();

    GetAdminServer()->Kill();
    GetGameServer()->Kill();
    GetServerManager()->Kill();

    GetLog()->Kill();
    GetContext()->Kill();

    mysql_library_end();

    SystemSignal::SignalShutdownComplete();
    return 0;
}
