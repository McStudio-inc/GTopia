#pragma once

#include "Event/EventDispatcher.h"
#include "Network/NetEntity.h"
#include "Network/NetSocket.h"
#include "Precompiled.h"
#include "Utils/Timer.h"

struct AdminClientConfig
{
    string account = "";
    string password = "";
    string displayName = "";
    int32 adminLevel = 0;
    std::vector<string> allowedIPs;

    bool IsTrustedIP(const string& ip)
    {
        for (auto& trustedIP : allowedIPs)
        {
            if (trustedIP == ip)
                return true;
        }
        return false;
    }
};

class AdminClient : public NetEntity
{
public:
    AdminClient(NetClient* pClient);
    ~AdminClient();

    void SendPacket(const string& payload);

    void SetAdminLevel(int32 adminLevel) { m_adminLevel = adminLevel; }
    int32 GetAdminLevel() const { return m_adminLevel; }

    void SetDisplayName(const string& displayName) { m_displayName = displayName; }
    string GetDisplayName() const { return m_displayName; }

    Timer& GetLastActionTime() { return m_lastActionTime; }
    void SetAuthed(bool authed) { m_authed = authed; }
    bool IsAuthed() const { return m_authed; }

    void CloseConnection()
    {
        if (m_pClient)
            m_pClient->status = SOCKET_CLIENT_CLOSE;
    }
    string GetIP() const { return m_pClient ? m_pClient->ip : ""; }

    void SetBusy(bool busy) { m_isBusy = busy; }
    bool IsBusy() const { return m_isBusy; }

private:
    NetClient* m_pClient;
    int32 m_adminLevel;
    Timer m_lastActionTime;
    string m_displayName;
    bool m_authed;
    uint8 m_passTryCount;
    bool m_isBusy;
};

class AdminServer
{
public:
    AdminServer();
    ~AdminServer();

public:
    static AdminServer* GetInstance()
    {
        static AdminServer instance;
        return &instance;
    }

public:
    bool Init();
    void Kill();
    void Update();

    bool IsEnabled() const { return m_isEnabled; }
    const string& GetHost() const { return m_host; }
    uint16 GetPort() const { return m_port; }

    void OnClientConnect(NetClient* pClient);
    void OnClientReceive(NetClient* pClient);
    void OnClientDisconnect(NetClient* pClient);

    bool LoadConfigFromFile(const string& filePath);
    AdminClientConfig* GetClientConfig(const string& account, const string& password);
    AdminClient* GetClientByName(const string& name);
    void RemoveClient(uint32 netID);

    bool IsTrustedIP(const string& ip);
    bool IsIPRateLimited(const string& ip);
    void ApplyRateLimit(const string& ip);

    void HandleCommand(AdminClient* pNetClient, const string& command);

    AdminClient* GetClientByNetID(uint32 netID);

private:
    string m_host;
    uint16 m_port;
    NetSocket* m_pNetSocket;
    bool m_skipIPCheck;
    Timer m_lastClientUpdateTime;
    bool m_isEnabled;

    std::vector<AdminClientConfig> m_clientConfig;
    std::unordered_map<uint32, AdminClient*> m_clients;
    std::vector<string> m_trustedIPs;
    std::unordered_map<string, Timer> m_rateLimits;
};

AdminServer* GetAdminServer();