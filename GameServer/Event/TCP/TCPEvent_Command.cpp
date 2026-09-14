#include "TCPEvent_Command.h"
#include "../../Player/PlayerManager.h"
#include "../../World/WorldManager.h"
#include "Player/RoleManager.h"

void TCPEvent_Command(NetClient* pClient, TCPPacketHeader& header, TCPPacketReader& reader)
{
    if (!pClient)
        return;

    int32 commandType = 0;
    if (!reader.Read<int32>(commandType))
        return;

    switch (commandType)
    {
        case TCP_COMMAND_SETROLE:
        {
            TCPEvent_Command_SetRole(reader);
            break;
        }

        case TCP_COMMAND_BROADCAST_MESSAGE:
        {
            TCPEvent_Command_BroadcastMessage(reader);
            break;
        }
    }
}

void TCPEvent_Command_SetRole(TCPPacketReader& reader)
{
    uint32 userID = 0;
    uint32 roleID = 0;

    if (!reader.Read<uint32>(userID) || !reader.Read<uint32>(roleID))
        return;

    GamePlayer* pPlayer = GetPlayerManager()->GetPlayerByUserID(userID);
    if (!pPlayer)
        return;

    Role* pRole = GetRoleManager()->GetRole(roleID);
    if (!pRole)
        return;

    pPlayer->SetRole(pRole);

    World* pWorld = GetWorldManager()->GetWorldByInstanceID(pPlayer->GetCurrentWorld());
    if (!pWorld)
        return;

    pWorld->SendPlayerDataConfigToAll(pPlayer);
    pWorld->SendNameChangeToAll(pPlayer);
}

void TCPEvent_Command_BroadcastMessage(TCPPacketReader& reader)
{
    string message;
    string worldName;
    string audio;

    if (!reader.ReadString(message) || !reader.ReadString(worldName) || !reader.ReadString(audio))
        return;

    GetPlayerManager()->BroadcastMessage(message, worldName, audio);
}
