#include "../Context.h"
#include "../Player/PlayerManager.h"
#include "../Server/ServerManager.h"
#include "CommandManager.h"
#include "Database/Table/PlayerDBTable.h"
#include "Player/RoleManager.h"

AdminCommandManager* GetAdminCommandManager()
{
    return AdminCommandManager::GetInstance();
}

static void SetRoleUpdateRoleCB(QueryTaskResult&& result)
{
    AdminClient* pNetClient = GetAdminServer()->GetClientByNetID(result.ownerID);
    if (!pNetClient)
        return;

    if (result.status != QUERY_STATUS_OK)
    {
        pNetClient->SendPacket("Failed update role id for user.");
    }
    else
    {
        pNetClient->SendPacket("Successfully updated player's role");

        Variant* pRoleID = result.GetExtraData(0);
        Variant* pUserID = result.GetExtraData(1);

        if (pRoleID && pUserID)
        {
            PlayerSession* pPlayerSession = GetPlayerManager()->GetSessionByID(pUserID->GetUINT());
            if (pPlayerSession)
            {
                ServerInfo* pServer = GetServerManager()->GetServerByID(pPlayerSession->serverID);
                if (pServer)
                {
                    GetServerManager()->SendCommandSetRole(pServer, pPlayerSession->userID, pRoleID->GetUINT());
                }
            }
        }
    }

    pNetClient->SetBusy(false);
}

static void SetRoleCheckPlayerCB(QueryTaskResult&& result)
{
    AdminClient* pNetClient = GetAdminServer()->GetClientByNetID(result.ownerID);
    if (!pNetClient)
        return;

    if (!result.result)
    {
        pNetClient->SendPacket("Error happened while checking user.");
        pNetClient->SetBusy(false);
        return;
    }

    if (result.result->GetRowCount() > 0)
    {
        Variant* pRoleID = result.GetExtraData(0);
        Variant* pUserID = result.GetExtraData(1);

        if (!pRoleID || !pUserID)
        {
            pNetClient->SendPacket("Oops! Something went wrong.");
            pNetClient->SetBusy(false);
            return;
        }

        QueryRequest req = PlayerDB::SetRoleByID(pRoleID->GetUINT(), pUserID->GetUINT(), pNetClient->GetNetID());
        req.AddExtraData(pRoleID->GetUINT(), pUserID->GetUINT());
        req.callback = &SetRoleUpdateRoleCB;

        DatabasePlayerExec(GetContext()->GetDatabasePool(), req);
    }
    else
    {
        pNetClient->SendPacket("User not found.");
        pNetClient->SetBusy(false);
        return;
    }
}

MAKE_COMMAND(SetRole, "/setrole <userID> <roleID>", "Set player's RoleID", 4, "setrole"_hash)
{
    if (!pNetClient || args.empty() || !CheckPerm(pNetClient))
        return;

    if (args.size() < 3)
    {
        SendUsage(pNetClient);
        return;
    }

    uint32 userID = 0;
    if (ToUInt(args[1], userID) != TO_INT_SUCCESS)
    {
        pNetClient->SendPacket("UserID must be number.");
        return;
    }

    if (userID == 0)
    {
        pNetClient->SendPacket("User not found.");
        return;
    }

    uint32 roleID = 0;
    if (ToUInt(args[2], roleID) != TO_INT_SUCCESS)
    {
        pNetClient->SendPacket("RoleID must be number.");
        return;
    }

    Role* pRole = GetRoleManager()->GetRole(roleID);
    if (!pRole)
    {
        pNetClient->SendPacket("RoleID not found.");
        return;
    }

    pNetClient->SetBusy(true);

    QueryRequest req = PlayerDB::ExistByID(userID, pNetClient->GetNetID());
    req.AddExtraData(roleID, userID);
    req.callback = &SetRoleCheckPlayerCB;

    DatabasePlayerExec(GetContext()->GetDatabasePool(), req);
}

MAKE_COMMAND(Shutdown, "/shutdown", "Shutdown all game servers and master server", 4, "shutdown"_hash)
{
    if (!pNetClient || !CheckPerm(pNetClient))
        return;

    pNetClient->SendPacket("Initiating server shutdown sequence...");

    GetServerManager()->ShutdownAllServers();
}

MAKE_COMMAND(Broadcast, "/broadcast <message>", "Broadcasts message to game servers", 3, "broadcast"_hash)
{
    if (!pNetClient || !CheckPerm(pNetClient))
        return;

    if (args.size() < 2)
    {
        SendUsage(pNetClient);
        return;
    }

    GetServerManager()->SendCommandBroadcastMessageToAll("`4Global System Message: `$" + args[1], "", "");
    pNetClient->SendPacket("Sent broadcast message to all game servers");
}

void AdminCommandManager::RegisterAllCommands()
{
    Register<Command_SetRole>();
    Register<Command_Shutdown>();
    Register<Command_Broadcast>();
}