#pragma once

#include "../Server/AdminServer.h"
#include "Event/EventDispatcher.h"
#include "IO/Log.h"
#include "Precompiled.h"
#include "Utils/StringUtils.h"

struct AdminCommandInfo
{
    string usage = "";
    string desc = "";
    int32 minAdminLevel = 999; // set it high
    std::vector<uint32> aliases;
};

template <typename T> class AdminCommandBase
{
public:
    static const AdminCommandInfo& GetInfo() { return T::GetInfo(); }

    static void Execute(AdminClient* pNetClient, std::vector<string>& args)
    {
        if (!CheckPerm(pNetClient))
            return;

        T::Execute(pNetClient, args);
    }

    static bool CheckPerm(AdminClient* pNetClient)
    {
        if (!pNetClient || pNetClient->GetAdminLevel() < GetInfo().minAdminLevel)
        {
            pNetClient->SendPacket("Unknown command.");
            return false;
        }

        return true;
    }

    static void SendUsage(AdminClient* pNetClient)
    {
        if (!pNetClient)
            return;

        pNetClient->SendPacket("Command usage: " + GetInfo().usage);
    }
};

class AdminCommandManager
{
public:
    AdminCommandManager() = default;
    ~AdminCommandManager() = default;

public:
    static AdminCommandManager* GetInstance()
    {
        static AdminCommandManager instance;
        return &instance;
    }

public:
    void RegisterAllCommands();

    void ExecuteCommand(AdminClient* pNetClient, std::vector<string>& args)
    {
        if (!pNetClient)
            return;

        if (pNetClient->GetAdminLevel() == 0 || args.empty())
        {
            pNetClient->SendPacket("Unknown command.");
            return;
        }

        if (args[0].size() < 2 || args[0][0] != '/')
        {
            pNetClient->SendPacket("Unknown command. Commands must start with '/'");
            return;
        }

        uint32 hashCmd = HashString(args[0].substr(1));
        if (!m_commands.HasHandler(hashCmd))
        {
            pNetClient->SendPacket("Unknown command.");
            return;
        }

        LOGGER_LOG_INFO("[Admin] Command executed by Name: %s (IP: %s) -> '%s'", pNetClient->GetDisplayName().c_str(),
                        pNetClient->GetIP().c_str(), JoinString(args, " ").c_str());

        m_commands.Dispatch(hashCmd, pNetClient, args);
    }

private:
    template <class T> void Register()
    {
        for (auto& alias : T::GetInfo().aliases)
        {
            m_commands.Register(alias, Delegate<AdminClient*, std::vector<string>&>::Create<&T::Execute>());
        }
    }

private:
    EventDispatcher<uint32, AdminClient*, std::vector<string>&> m_commands;
};

AdminCommandManager* GetAdminCommandManager();

#define MAKE_COMMAND(Name, Usage, Desc, Perm, ...)                                                                     \
    class Command_##Name : public AdminCommandBase<Command_##Name>                                                     \
    {                                                                                                                  \
    public:                                                                                                            \
        static const AdminCommandInfo& GetInfo()                                                                       \
        {                                                                                                              \
            static AdminCommandInfo info = {Usage, Desc, Perm, {__VA_ARGS__}};                                         \
            return info;                                                                                               \
        }                                                                                                              \
        static void Execute(AdminClient* pNetClient, std::vector<string>& args);                                       \
    };                                                                                                                 \
    void Command_##Name::Execute(AdminClient* pNetClient, std::vector<string>& args)
