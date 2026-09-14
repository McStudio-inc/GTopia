#include "AdminServer.h"
#include "../Command/CommandManager.h"
#include "IO/Log.h"
#include "Utils/ConfigDB.h"
#include "Utils/StringUtils.h"

AdminClient::AdminClient(NetClient* pClient)
    : NetEntity(ENTITY_TYPE_TELNET), m_adminLevel(0), m_displayName("Unknown"), m_authed(false), m_isBusy(false)
{
    m_pClient = pClient;
}

AdminClient::~AdminClient() {}

void AdminClient::SendPacket(const string& payload)
{
    if (!m_pClient)
        return;

    uint32 totalSize = sizeof(uint32) + payload.size();
    std::vector<uint8> buffer(totalSize); // lets go with vec for now, not membuffer

    memcpy(buffer.data(), &totalSize, sizeof(uint32));
    if (!payload.empty())
    {
        memcpy(buffer.data() + sizeof(uint32), payload.data(), payload.size());
    }

    m_pClient->Send(buffer.data(), buffer.size());
}

AdminServer::AdminServer() : m_pNetSocket(nullptr), m_port(0), m_skipIPCheck(false) {}
AdminServer::~AdminServer() {}

bool AdminServer::Init()
{
    SAFE_DELETE(m_pNetSocket);

    if (m_host.empty() || m_port == 0)
        return false;

    m_pNetSocket = new NetSocket();

    if (!m_pNetSocket->Init(m_host, m_port, 100))
        return false;

    m_pNetSocket->GetEvents().Register(SOCKET_EVENT_TYPE_RECEIVE,
                                       Delegate<NetClient*>::Create<AdminServer, &AdminServer::OnClientReceive>(this));
    m_pNetSocket->GetEvents().Register(SOCKET_EVENT_TYPE_CONNECT,
                                       Delegate<NetClient*>::Create<AdminServer, &AdminServer::OnClientConnect>(this));
    m_pNetSocket->GetEvents().Register(
        SOCKET_EVENT_TYPE_DISCONNECT,
        Delegate<NetClient*>::Create<AdminServer, &AdminServer::OnClientDisconnect>(this));

    GetAdminCommandManager()->RegisterAllCommands();
    return true;
}

void AdminServer::Kill()
{
    SAFE_DELETE(m_pNetSocket);

    for (auto& [_, pNetClient] : m_clients)
    {
        SAFE_DELETE(pNetClient);
    }
    m_clients.clear();
}

void AdminServer::Update()
{
    if (!m_pNetSocket)
        return;

    m_pNetSocket->Update(false);

    if (m_lastClientUpdateTime.GetElapsedTime() <= 5000)
        return;

    for (auto& [_, pNetClient] : m_clients)
    {
        if (!pNetClient)
            continue;

        if ((pNetClient->IsAuthed() && pNetClient->GetLastActionTime().GetElapsedTime() >= 3600 * 1000) ||
            (!pNetClient->IsAuthed() && pNetClient->GetLastActionTime().GetElapsedTime() >= 60 * 1000))
        {
            LOGGER_LOG_INFO("[Admin] Session timeout for IP: %s Name: %s - closing connection",
                            pNetClient->GetIP().c_str(), pNetClient->GetDisplayName().c_str());
            pNetClient->CloseConnection();
        }
    }

    m_lastClientUpdateTime.Reset();
}

void AdminServer::OnClientConnect(NetClient* pClient)
{
    if (!pClient)
        return;

    if (pClient->ip.empty() || pClient->data)
    {
        pClient->status = SOCKET_CLIENT_CLOSE;
        return;
    }

    if (IsIPRateLimited(pClient->ip))
    {
        LOGGER_LOG_WARN("[Admin] Connection rejected: IP %s is currently rate limited", pClient->ip.c_str());
        pClient->status = SOCKET_CLIENT_CLOSE;
        return;
    }

    if (!m_skipIPCheck && !IsTrustedIP(pClient->ip))
    {
        LOGGER_LOG_WARN("[Admin] Connection rejected: IP %s is not in trusted list", pClient->ip.c_str());
        pClient->status = SOCKET_CLIENT_CLOSE;
        return;
    }

    AdminClient* pNetClient = new AdminClient(pClient);
    pClient->data = pNetClient;

    m_clients.insert_or_assign(pNetClient->GetNetID(), pNetClient);
    LOGGER_LOG_INFO("[Admin] New Connection established from IP: %s (NetID: %u)", pClient->ip.c_str(),
                    pNetClient->GetNetID());
}

void AdminServer::OnClientReceive(NetClient* pClient)
{
    if (!pClient)
        return;

    AdminClient* pNetClient = (AdminClient*)(pClient->data);
    if (!pNetClient)
    {
        pClient->status = SOCKET_CLIENT_CLOSE;
        return;
    }

    while (true)
    {
        std::string payload;

        {
            std::lock_guard<std::mutex> lock(pClient->recvMutex);

            if (pClient->recvQueue.GetDataSize() < sizeof(uint32))
                return;

            uint32 totalSize = 0;
            pClient->recvQueue.Peek((uint8*)(&totalSize), sizeof(uint32));

            if (totalSize < sizeof(uint32) || totalSize > 4 * 1024 * 1024)
            {
                LOGGER_LOG_WARN("[Admin] Malformed packet size (%u bytes) received from IP: %s Name: %s", totalSize,
                                pNetClient->GetIP().c_str(), pNetClient->GetDisplayName().c_str());

                pClient->status = SOCKET_CLIENT_CLOSE;
                return;
            }

            if (pClient->recvQueue.GetDataSize() < totalSize)
                return;

            pClient->recvQueue.Read((uint8*)(&totalSize), sizeof(uint32));

            uint32 payloadSize = totalSize - sizeof(uint32);

            if (payloadSize > 0)
            {
                payload.resize(payloadSize);
                pClient->recvQueue.Read(payload.data(), payloadSize);
            }
        }

        HandleCommand(pNetClient, payload);

        if (pClient->status == SOCKET_CLIENT_CLOSE)
            return;
    }
}

void AdminServer::OnClientDisconnect(NetClient* pClient)
{
    if (!pClient)
        return;

    AdminClient* pNetClient = (AdminClient*)(pClient->data);
    if (!pNetClient)
        return;

    LOGGER_LOG_INFO("[Admin] Client disconnected IP: %s, Name: %s, NetID: %u, Authed: %s", pNetClient->GetIP().c_str(),
                    pNetClient->GetDisplayName().c_str(), pNetClient->GetNetID(),
                    pNetClient->IsAuthed() ? "Yes" : "No");

    RemoveClient(pNetClient->GetNetID());
}

void AdminServer::HandleCommand(AdminClient* pNetClient, const string& payload)
{
    if (!pNetClient)
        return;

    if (!pNetClient->IsAuthed())
    {
        auto args = Split(payload, ' ');

        if (args.size() != 3 || args[0] != "auth")
        {
            pNetClient->SendPacket("Authentication failed");
            ApplyRateLimit(pNetClient->GetIP());
            LOGGER_LOG_WARN("[Admin] Auth failed (Invalid Format) from IP: %s, Rate limit applied, closing socket",
                            pNetClient->GetIP().c_str());
            pNetClient->CloseConnection();
            return;
        }

        const string& account = args[1];
        const string& password = args[2];

        AdminClientConfig* pConfig = GetClientConfig(account, password);
        if (!pConfig)
        {
            pNetClient->SendPacket("Authentication failed");
            ApplyRateLimit(pNetClient->GetIP());
            LOGGER_LOG_WARN("[Admin] Auth failed (Invalid Credentials) for account '%s' from IP: %s, Rate limit "
                            "applied, closing socket",
                            account.c_str(), pNetClient->GetIP().c_str());
            pNetClient->CloseConnection();
            return;
        }

        if (!m_skipIPCheck && !pConfig->IsTrustedIP(pNetClient->GetIP()))
        {
            pNetClient->SendPacket("Authentication failed");
            ApplyRateLimit(pNetClient->GetIP());
            LOGGER_LOG_WARN(
                "[Admin] Auth failed (Untrusted IP) for account '%s' from IP: %s, Rate limit applied, closing socket",
                pConfig->account.c_str(), pNetClient->GetIP().c_str());
            pNetClient->CloseConnection();
            return;
        }

        AdminClient* pTarget = GetClientByName(pConfig->displayName);
        if (pTarget)
        {
            pNetClient->SendPacket("Authentication failed");
            LOGGER_LOG_WARN("[Admin] Auth failed for account '%s' from IP: %s, Already online from IP: %s",
                            pConfig->displayName.c_str(), pNetClient->GetIP().c_str(), pTarget->GetIP().c_str());
            pNetClient->CloseConnection();
            return;
        }

        pNetClient->SetDisplayName(pConfig->displayName);
        pNetClient->SetAdminLevel(pConfig->adminLevel);
        pNetClient->SetAuthed(true);

        pNetClient->SendPacket("OK");
        LOGGER_LOG_INFO("[Admin] Client authenticated successfully -> IP: %s | Name: %s | Account: %s | AdminLevel: %d",
                        pNetClient->GetIP().c_str(), pNetClient->GetDisplayName().c_str(), account.c_str(),
                        pNetClient->GetAdminLevel());
        return;
    }

    if (pNetClient->GetLastActionTime().GetElapsedTime() <= 200)
    {
        pNetClient->SendPacket("[ERROR] Rate limit exceeded");
        return;
    }
    pNetClient->GetLastActionTime().Reset();

    if (payload.empty())
        return;

    auto args = Split(payload, ' ');
    GetAdminCommandManager()->ExecuteCommand(pNetClient, args);
}

AdminClient* AdminServer::GetClientByNetID(uint32 netID)
{
    auto it = m_clients.find(netID);
    if (it != m_clients.end())
        return it->second;
    return nullptr;
}

bool AdminServer::LoadConfigFromFile(const string& filePath)
{
    ConfigDB cfg;
    if (!cfg.Load(filePath))
        return false;

    for (auto& line : cfg.Lines())
    {
        const string& key = line.GetString(0);

        if (key == "enable_admin_server")
        {
            if (!line.Require(1))
                return false;
            m_isEnabled = line.GetUInt(1) == 1 ? true : false;

            if (!m_isEnabled)
                return true;
        }

        if (key == "admin_host")
        {
            if (!line.Require(1))
                return false;
            m_host = line.GetString(1);
        }

        if (key == "admin_port")
        {
            if (!line.Require(1))
                return false;
            m_port = line.GetUInt(1);
        }

        if (key == "skip_ip_check")
        {
            m_skipIPCheck = line.GetUInt(1) != 0;
        }

        if (key == "add_account")
        {
            if (!line.Require(4))
                return false;

            AdminClientConfig config;
            config.displayName = line.GetString(1);
            config.account = line.GetString(2);
            config.password = line.GetString(3);
            config.adminLevel = line.GetInt(4);

            m_clientConfig.push_back(std::move(config));
        }

        if (key == "allow_ip")
        {
            if (m_clientConfig.empty())
                continue;

            for (uint8 i = 1; i < line.GetArgSize(); ++i)
            {
                const string& ip = line.GetString(i);
                if (ip.empty())
                    continue;

                m_clientConfig.back().allowedIPs.push_back(ip);

                if (!m_skipIPCheck)
                {
                    bool ipExists = false;
                    for (auto& trustedIP : m_trustedIPs)
                    {
                        if (trustedIP == ip)
                        {
                            ipExists = true;
                            break;
                        }
                    }
                    if (!ipExists)
                        m_trustedIPs.push_back(ip);
                }
            }
        }
    }

    return true;
}

AdminClientConfig* AdminServer::GetClientConfig(const string& account, const string& password)
{
    for (auto& client : m_clientConfig)
    {
        if (client.account == account && client.password == password)
        {
            return &client;
        }
    }
    return nullptr;
}

AdminClient* AdminServer::GetClientByName(const string& name)
{
    for (auto& [_, pNetClient] : m_clients)
    {
        if (pNetClient && pNetClient->GetDisplayName() == name)
            return pNetClient;
    }
    return nullptr;
}

void AdminServer::RemoveClient(uint32 netID)
{
    auto it = m_clients.find(netID);
    if (it == m_clients.end())
        return;

    AdminClient* pNetClient = it->second;
    if (!pNetClient)
        return;

    pNetClient->CloseConnection();
    SAFE_DELETE(pNetClient);
    m_clients.erase(it);
}

bool AdminServer::IsTrustedIP(const string& ip)
{
    for (auto& trustedIP : m_trustedIPs)
    {
        if (trustedIP == ip)
            return true;
    }
    return false;
}

bool AdminServer::IsIPRateLimited(const string& ip)
{
    auto it = m_rateLimits.find(ip);
    if (it == m_rateLimits.end())
        return false;

    if (it->second.GetElapsedTime() <= 10 * 1000)
        return true;

    m_rateLimits.erase(it);
    return false;
}

void AdminServer::ApplyRateLimit(const string& ip)
{
    auto it = m_rateLimits.find(ip);
    if (it != m_rateLimits.end())
    {
        it->second.Reset();
        return;
    }
    m_rateLimits.insert_or_assign(ip, Timer());
}

AdminServer* GetAdminServer()
{
    return AdminServer::GetInstance();
}