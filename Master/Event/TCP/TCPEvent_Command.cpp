#include "TCPEvent_Command.h"
#include "../../Server/ServerManager.h"

void TCPEvent_Command(NetClient* pClient, TCPPacketHeader& header, TCPPacketReader& reader)
{
    if (!pClient)
        return;

    int32 commandType = 0;
    if (!reader.Read<int32>(commandType))
        return;

    switch (commandType)
    {
        case TCP_COMMAND_BROADCAST_MESSAGE:
        {
            TCPEvent_Command_BroadcastMessage(reader);
            break;
        }
    }
}

void TCPEvent_Command_BroadcastMessage(TCPPacketReader& reader)
{
    string message;
    string worldName;
    string audio;

    if (!reader.ReadString(message) || !reader.ReadString(message) || !reader.ReadString(audio))
        return;

    GetServerManager()->SendCommandBroadcastMessageToAll(message, worldName, audio);
}
