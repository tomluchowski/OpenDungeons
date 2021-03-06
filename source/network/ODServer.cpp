/*
 *  Copyright (C) 2011-2014  OpenDungeons Team
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "network/ODServer.h"

#include "network/ODClient.h"
#include "network/ServerNotification.h"
#include "gamemap/GameMap.h"
#include "ODApplication.h"
#include "entities/MapLight.h"
#include "network/ChatMessage.h"
#include "modes/ODConsoleCommand.h"
#include "gamemap/MapLoader.h"
#include "utils/LogManager.h"
#include "entities/Creature.h"
#include "entities/Weapon.h"
#include "traps/Trap.h"
#include "traps/TrapCannon.h"
#include "traps/TrapSpike.h"
#include "traps/TrapBoulder.h"
#include "rooms/RoomCrypt.h"
#include "rooms/RoomDormitory.h"
#include "rooms/RoomDungeonTemple.h"
#include "rooms/RoomForge.h"
#include "rooms/RoomHatchery.h"
#include "rooms/RoomLibrary.h"
#include "rooms/RoomPortal.h"
#include "rooms/RoomTrainingHall.h"
#include "rooms/RoomTreasury.h"
#include "utils/ConfigManager.h"

#include <SFML/Network.hpp>
#include <SFML/System.hpp>

#include "boost/filesystem.hpp"

const std::string ODServer::SERVER_INFORMATION = "SERVER_INFORMATION";

template<> ODServer* Ogre::Singleton<ODServer>::msSingleton = 0;

ODServer::ODServer() :
    mServerMode(ServerMode::ModeNone),
    mServerState(ServerState::StateNone),
    mGameMap(new GameMap(true)),
    mSeatsConfigured(false)
{
}

ODServer::~ODServer()
{
    delete mGameMap;
}

bool ODServer::startServer(const std::string& levelFilename, ServerMode mode)
{
    LogManager& logManager = LogManager::getSingleton();
    logManager.logMessage("Asked to launch server with levelFilename=" + levelFilename);

    mSeatsConfigured = false;

    // Start the server socket listener as well as the server socket thread
    if (isConnected())
    {
        logManager.logMessage("Couldn't start server: The server is already connected");
        return false;
    }
    if (ODClient::getSingleton().isConnected())
    {
        logManager.logMessage("Couldn't start server: The client is already connected");
        return false;
    }

    // Read in the map. The map loading should be happen here and not in the server thread to
    // make sure it is valid before launching the server.
    mServerMode = mode;
    mServerState = ServerState::StateConfiguration;
    GameMap* gameMap = mGameMap;
    if (!gameMap->loadLevel(levelFilename))
    {
        mServerMode = ServerMode::ModeNone;
        mServerState = ServerState::StateNone;
        logManager.logMessage("Couldn't start server. The level file can't be loaded: " + levelFilename);
        return false;
    }

    // Set up the socket to listen on the specified port
    if (!createServer(ConfigManager::getSingleton().getNetworkPort()))
    {
        mServerMode = ServerMode::ModeNone;
        mServerState = ServerState::StateNone;
        logManager.logMessage("ERROR:  Server could not create server socket!");
        return false;
    }

    return true;
}

void ODServer::queueServerNotification(ServerNotification* n)
{
    if ((n == NULL) || (!isConnected()))
        return;
    mServerNotificationQueue.push_back(n);
}

void ODServer::sendAsyncMsg(ServerNotification& notif)
{
    sendMsg(notif.mConcernedPlayer, notif.mPacket);
}

void ODServer::sendMsg(Player* player, ODPacket& packet)
{
    if(player == nullptr)
    {
        // If player is nullptr, we send the message to every connected player
        for (ODSocketClient* client : mSockClients)
            sendMsgToClient(client, packet);

        return;
    }

    ODSocketClient* client = getClientFromPlayer(player);
    OD_ASSERT_TRUE_MSG(client != nullptr, "player=" + player->getNick());
    if(client == nullptr)
        return;

    sendMsgToClient(client, packet);
}

void ODServer::queueConsoleCommand(ODConsoleCommand* cc)
{
    if ((cc == NULL) || (!isConnected()))
        return;
    mConsoleCommandQueue.push_back(cc);
}

void ODServer::processServerCommandQueue()
{
    bool running = true;
    GameMap* gameMap = mGameMap;

    while(running)
    {
        // If the queue is empty, let's get out of the loop.
        if (mConsoleCommandQueue.empty())
            break;

        // Take a message out of the front of the notification queue
        ODConsoleCommand *command = mConsoleCommandQueue.front();
        mConsoleCommandQueue.pop_front();

        if (command == NULL)
            continue;

        command->execute(gameMap);

        delete command;
    }
}

void ODServer::startNewTurn(double timeSinceLastFrame)
{
    GameMap* gameMap = mGameMap;
    int64_t turn = gameMap->getTurnNumber();

    // We wait until every client acknowledge the turn to start the next one. This way, we ensure
    // synchronisation is not too bad
    for (ODSocketClient* client : mSockClients)
    {
        if(client->getLastTurnAck() != turn)
            return;
    }

    gameMap->setTurnNumber(++turn);

    ServerNotification* serverNotification = new ServerNotification(
        ServerNotification::turnStarted, nullptr);
    serverNotification->mPacket << turn;
    queueServerNotification(serverNotification);

    gameMap->updateAnimations(timeSinceLastFrame);

    // We notify the clients about what they got
    for (ODSocketClient* sock : mSockClients)
    {
        Player* player = sock->getPlayer();
        // For now, only the player whose seat changed is notified. If we need it, we could send the event to every player
        // so that they can see how far from the goals the other players are
        ServerNotification *serverNotification = new ServerNotification(
            ServerNotification::refreshPlayerSeat, player);
        std::string goals = gameMap->getGoalsStringForPlayer(player);
        Seat* seat = player->getSeat();
        seat->computeSeatBeforeSendingToClient();
        serverNotification->mPacket << seat << goals;
        ODServer::getSingleton().queueServerNotification(serverNotification);

        // Here, the creature list is pulled. It could be possible that the creature dies before the stat window is
        // closed. So, if we cannot find the creature, we just erase it.
        std::vector<std::string>& creatures = mCreaturesInfoWanted[sock];
        std::vector<std::string>::iterator itCreatures = creatures.begin();
        while(itCreatures != creatures.end())
        {
            std::string& name = *itCreatures;
            Creature* creature = gameMap->getCreature(name);
            if(creature == nullptr)
                itCreatures = creatures.erase(itCreatures);
            else
            {
                std::string creatureInfos = creature->getStatsText();

                ServerNotification *serverNotification = new ServerNotification(
                    ServerNotification::notifyCreatureInfo, player);
                serverNotification->mPacket << name << creatureInfos;
                ODServer::getSingleton().queueServerNotification(serverNotification);

                ++itCreatures;
            }
        }
    }

    switch(mServerMode)
    {
        case ServerMode::ModeGameSinglePlayer:
        case ServerMode::ModeGameMultiPlayer:
        {
            gameMap->doTurn();
            gameMap->doPlayerAITurn(timeSinceLastFrame);
            break;
        }
        case ServerMode::ModeEditor:
            // We do not update turn in editor mode to not have creatures wander
            break;
        case ServerMode::ModeNone:
            // It is not normal to have no mode selected and starting turns
            OD_ASSERT_TRUE(false);
            break;
    }

    gameMap->updateVisibleEntities();
    gameMap->processDeletionQueues();
}

void ODServer::serverThread()
{
    GameMap* gameMap = mGameMap;
    sf::Clock clock;
    double turnLengthMs = 1000.0 / ODApplication::turnsPerSecond;
    while(isConnected())
    {
        // doTask sould return after the length of 1 turn even if their are communications. When
        // it returns, we can launch next turn.
        doTask(static_cast<int32_t>(turnLengthMs));

        if(gameMap->getTurnNumber() == -1)
        {
            // The game is not started
            if(mSeatsConfigured)
            {
                const std::vector<Seat*>& seats = gameMap->getSeats();
                for (int jj = 0; jj < gameMap->getMapSizeY(); ++jj)
                {
                    for (int ii = 0; ii < gameMap->getMapSizeX(); ++ii)
                    {
                        Tile* tile = gameMap->getTile(ii,jj);
                        tile->setSeats(seats);
                    }
                }

                // We set allied seats
                for(Seat* seat : seats)
                {
                    for(Seat* alliedSeat : seats)
                    {
                        if(alliedSeat == seat)
                            continue;
                        if(!seat->isAlliedSeat(alliedSeat))
                            continue;
                        seat->addAlliedSeat(alliedSeat);
                    }
                }

                // Every client is connected and ready, we can launch the game
                // Send turn 0 to init the map
                ServerNotification* serverNotification = new ServerNotification(
                    ServerNotification::turnStarted, nullptr);
                serverNotification->mPacket << static_cast<int64_t>(0);
                queueServerNotification(serverNotification);

                LogManager::getSingleton().logMessage("Server ready, starting game");
                gameMap->setTurnNumber(0);
                gameMap->setGamePaused(false);

                // In editor mode, we give vision on all the gamemap tiles
                if(mServerMode == ServerMode::ModeEditor)
                {
                    for (int jj = 0; jj < gameMap->getMapSizeY(); ++jj)
                    {
                        for (int ii = 0; ii < gameMap->getMapSizeX(); ++ii)
                        {
                            for (Seat* seat : gameMap->getSeats())
                            {
                                gameMap->getTile(ii,jj)->notifyVision(seat);
                            }
                        }
                    }
                }

                gameMap->createAllEntities();

                // Fill starting gold
                for(Seat* seat : gameMap->getSeats())
                {
                    if(seat->getPlayer() == nullptr)
                        continue;

                    if(seat->getStartingGold() > 0)
                        gameMap->addGoldToSeat(seat->getStartingGold(), seat->getId());
                }
            }
            else
            {
                // We are still waiting for players
                continue;
            }
        }

        // After starting a new turn, we should process server notifications
        // before processing client messages. Otherwise, we could have weird issues
        // like allow picking up a dead creature for example.
        // We make sure the server time is a little bit late regarding the clients to
        // make sure server is not more advanced than clients. We do that because it is better for clients
        // to wait for server. If server is in advance, he might send commands before the
        // creatures arrive at their destination. That could result in weird issues like
        // creatures going through walls.
        startNewTurn(static_cast<double>(clock.restart().asSeconds()) * 0.95);

        processServerNotifications();

        processServerCommandQueue();
    }
}

void ODServer::processServerNotifications()
{
    GameMap* gameMap = mGameMap;

    bool running = true;

    while (running)
    {
        // If the queue is empty, let's get out of the loop.
        if (mServerNotificationQueue.empty())
            break;

        // Take a message out of the front of the notification queue
        ServerNotification *event = mServerNotificationQueue.front();
        mServerNotificationQueue.pop_front();

        OD_ASSERT_TRUE(event != nullptr);
        if(event == nullptr)
            continue;

        switch (event->mType)
        {
            case ServerNotification::turnStarted:
                LogManager::getSingleton().logMessage("Server sends newturn="
                    + Ogre::StringConverter::toString((int32_t)gameMap->getTurnNumber()));
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;

            case ServerNotification::setTurnsPerSecond:
                // This one is not used on client side. Shall we remove it?
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;

            case ServerNotification::entityPickedUp:
                // This message should not be sent by human players (they are notified asynchronously)
                OD_ASSERT_TRUE_MSG(event->mConcernedPlayer->getIsHuman(), "nick=" + event->mConcernedPlayer->getNick());
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;

            case ServerNotification::entityDropped:
                // This message should not be sent by human players (they are notified asynchronously)
                OD_ASSERT_TRUE_MSG(event->mConcernedPlayer->getIsHuman(), "nick=" + event->mConcernedPlayer->getNick());
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;

            case ServerNotification::entitySlapped:
                // This message should not be sent by human players (they are notified asynchronously)
                OD_ASSERT_TRUE_MSG(!event->mConcernedPlayer->getIsHuman(), "nick=" + event->mConcernedPlayer->getNick());
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;

            case ServerNotification::exit:
                running = false;
                stopServer();
                break;

            default:
                sendMsg(event->mConcernedPlayer, event->mPacket);
                break;
        }

        delete event;
        event = NULL;
    }
}

bool ODServer::processClientNotifications(ODSocketClient* clientSocket)
{
    if (!clientSocket)
        return false;

    GameMap* gameMap = mGameMap;

    ODPacket packetReceived;

    ODSocketClient::ODComStatus status = receiveMsgFromClient(clientSocket, packetReceived);

    // If the client closed the connection
    if (status != ODSocketClient::ODComStatus::OK)
    {
        // If a client disconnects during seat configuration, we delete him from the list
        if(mServerState != ServerState::StateConfiguration)
            return (status != ODSocketClient::ODComStatus::Error);

        // If the client is in a state where he has been notified to the other clients,
        // we notify his deconnexion
        if(std::string("ready").compare(clientSocket->getState()) != 0)
            return (status != ODSocketClient::ODComStatus::Error);

        // We notify
        uint32_t nbPlayers = 1;
        ODPacket packetSend;
        packetSend << ServerNotification::removePlayers << nbPlayers;
        int32_t id = clientSocket->getPlayer()->getId();
        packetSend << id;
        sendMsg(nullptr, packetSend);
        return (status != ODSocketClient::ODComStatus::Error);
    }

    ClientNotification::ClientNotificationType clientCommand;
    OD_ASSERT_TRUE(packetReceived >> clientCommand);

    switch(clientCommand)
    {
        case ClientNotification::hello:
        {
            if(std::string("connected").compare(clientSocket->getState()) != 0)
                return false;
            std::string version;
            OD_ASSERT_TRUE(packetReceived >> version);

            // If the version is different, we refuse the client
            if(version.compare(std::string("OpenDungeons V ") + ODApplication::VERSION) != 0)
            {
                LogManager::getSingleton().logMessage("Server rejected client. Application version mismatch: required= "
                    + ODApplication::VERSION + ", received=" + version);
                return false;
            }

            // Tell the client to load the given map
            LogManager::getSingleton().logMessage("Level relative path sent to client: " + gameMap->getLevelFileName());
            setClientState(clientSocket, "loadLevel");
            int32_t mapSizeX = gameMap->getMapSizeX();
            int32_t mapSizeY = gameMap->getMapSizeY();
            ODPacket packetSend;
            packetSend << ServerNotification::loadLevel << mapSizeX << mapSizeY;
            // Map infos
            packetSend << gameMap->getLevelFileName();
            packetSend << gameMap->getLevelDescription();
            packetSend << gameMap->getLevelMusicFile();
            packetSend << gameMap->getLevelFightMusicFile();

            int32_t nb;
            // Creature definitions
            nb = gameMap->numClassDescriptions();
            packetSend << nb;
            for(int32_t i = 0; i < nb; ++i)
            {
                const CreatureDefinition* def = gameMap->getClassDescription(i);
                packetSend << def;
            }

            // Weapons
            nb = gameMap->numWeapons();
            packetSend << nb;
            for(int32_t i = 0; i < nb; ++i)
            {
                const Weapon* def = gameMap->getWeapon(i);
                packetSend << def;
            }

            // Seats
            const std::vector<Seat*>& seats = gameMap->getSeats();
            nb = seats.size();
            packetSend << nb;
            for(Seat* seat : seats)
            {
                packetSend << seat;
            }

            // Tiles
            std::vector<Tile*> goldTiles;
            std::vector<Tile*> rockTiles;
            for (int jj = 0; jj < mapSizeX; ++jj)
            {
                for (int ii = 0; ii < mapSizeY; ++ii)
                {
                    Tile* tile = gameMap->getTile(ii,jj);
                    if(tile->getType() == Tile::TileType::gold)
                    {
                        goldTiles.push_back(tile);
                    }
                    else if(tile->getType() == Tile::TileType::rock)
                    {
                        rockTiles.push_back(tile);
                    }
                }
            }

            double fullness;
            nb = goldTiles.size();
            packetSend << nb;
            for(Tile* tile : goldTiles)
            {
                gameMap->tileToPacket(packetSend, tile);
                fullness = tile->getFullness();
                packetSend << fullness;
            }

            nb = rockTiles.size();
            packetSend << nb;
            for(Tile* tile : rockTiles)
            {
                gameMap->tileToPacket(packetSend, tile);
            }

            // Lights
            nb = gameMap->numMapLights();
            packetSend << nb;
            for(int32_t i = 0; i < nb; ++i)
            {
                MapLight* light = gameMap->getMapLight(i);
                packetSend << light;
            }

            sendMsgToClient(clientSocket, packetSend);
            break;
        }

        case ClientNotification::levelOK:
        {
            if(std::string("loadLevel").compare(clientSocket->getState()) != 0)
                return false;

            setClientState(clientSocket, "nick");
            // Tell the client to give us their nickname
            ODPacket packetSend;
            packetSend << ServerNotification::pickNick << mServerMode;
            sendMsgToClient(clientSocket, packetSend);
            break;
        }

        case ClientNotification::setNick:
        {
            if(std::string("nick").compare(clientSocket->getState()) != 0)
                return false;

            // Pick nick
            std::string clientNick;
            OD_ASSERT_TRUE(packetReceived >> clientNick);

            // NOTE : playerId 0 is reserved for inactive players and 1 is reserved for AI
            int32_t playerId = mSockClients.size() + 10;
            Player* curPlayer = new Player(gameMap, playerId);
            curPlayer->setNick(clientNick);
            curPlayer->setIsHuman(true);
            clientSocket->setPlayer(curPlayer);
            setClientState(clientSocket, "ready");

            if(mServerMode != ServerMode::ModeEditor)
                break;

            // On editor mode, we configure automatically seats
            mServerState = ServerState::StateGame;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            OD_ASSERT_TRUE(!seats.empty());
            if(seats.empty())
                break;

            Seat* seat = seats[0];
            seat->setPlayer(curPlayer);
            ODPacket packetSend;
            packetSend << ServerNotification::clientAccepted << ODApplication::turnsPerSecond;
            int32_t nbPlayers = 1;
            packetSend << nbPlayers;
            const std::string& nick = clientSocket->getPlayer()->getNick();
            int32_t id = clientSocket->getPlayer()->getId();
            int seatId = seat->getId();
            packetSend << nick << id << seatId;
            sendMsgToClient(clientSocket, packetSend);

            packetSend.clear();
            packetSend << ServerNotification::startGameMode << seatId << mServerMode;
            sendMsgToClient(clientSocket, packetSend);
            mSeatsConfigured = true;
            break;
        }

        case ClientNotification::readyForSeatConfiguration:
        {
            if(std::string("ready").compare(clientSocket->getState()) != 0)
                return false;

            ODPacket packetSend;
            // We notify to the newly connected player all the currently connected players (including himself
            uint32_t nbPlayers = mSockClients.size();
            packetSend << ServerNotification::addPlayers << nbPlayers;
            for (ODSocketClient* client : mSockClients)
            {
                const std::string& nick = client->getPlayer()->getNick();
                int32_t id = client->getPlayer()->getId();
                packetSend << nick << id;
            }
            sendMsgToClient(clientSocket, packetSend);

            // Then, we notify the newly connected client to every client
            const std::string& clientNick = clientSocket->getPlayer()->getNick();
            int32_t clientPlayerId = clientSocket->getPlayer()->getId();
            packetSend.clear();
            nbPlayers = 1;
            packetSend << ServerNotification::addPlayers << nbPlayers;
            packetSend << clientNick << clientPlayerId;
            for (ODSocketClient* client : mSockClients)
            {
                if(clientSocket == client)
                    continue;

                sendMsgToClient(client, packetSend);
            }
            break;
        }

        case ClientNotification::seatConfigurationRefresh:
        {
            std::vector<Seat*> seats = gameMap->getSeats();
            ODPacket packetSend;
            packetSend << ServerNotification::seatConfigurationRefresh;
            for(Seat* seat : seats)
            {
                int seatId;
                bool isSet;
                OD_ASSERT_TRUE(packetReceived >> seatId);
                OD_ASSERT_TRUE(seatId == seat->getId());
                packetSend << seatId;

                OD_ASSERT_TRUE(packetReceived >> isSet);
                packetSend << isSet;
                if(isSet)
                {
                    uint32_t factionIndex;
                    OD_ASSERT_TRUE(packetReceived >> factionIndex);
                    packetSend << factionIndex;
                }
                OD_ASSERT_TRUE(packetReceived >> isSet);
                packetSend << isSet;
                if(isSet)
                {
                    int32_t playerId;
                    OD_ASSERT_TRUE(packetReceived >> playerId);
                    packetSend << playerId;
                }
                OD_ASSERT_TRUE(packetReceived >> isSet);
                packetSend << isSet;
                if(isSet)
                {
                    int32_t teamId;
                    OD_ASSERT_TRUE(packetReceived >> teamId);
                    packetSend << teamId;
                }
            }
            sendMsg(nullptr, packetSend);
            break;
        }

        case ClientNotification::seatConfigurationSet:
        {
            // We change server state to make sure no new client will be accepted
            OD_ASSERT_TRUE_MSG(mServerState == ServerState::StateConfiguration, "Wrong server state="
                + Ogre::StringConverter::toString(static_cast<int>(mServerState)));
            mServerState = ServerState::StateGame;

            std::vector<Seat*> seats = gameMap->getSeats();
            const std::vector<std::string>& factions = ConfigManager::getSingleton().getFactions();
            for(Seat* seat : seats)
            {
                int seatId;
                bool isSet;
                uint32_t factionIndex;
                int32_t playerId;
                int32_t teamId;
                OD_ASSERT_TRUE(packetReceived >> seatId);
                OD_ASSERT_TRUE(seatId == seat->getId());
                OD_ASSERT_TRUE(packetReceived >> isSet);
                // It is not acceptable to have an incompletely configured seat
                OD_ASSERT_TRUE(isSet);
                OD_ASSERT_TRUE(packetReceived >> factionIndex);
                OD_ASSERT_TRUE(factionIndex < factions.size());
                seat->setFaction(factions[factionIndex]);

                OD_ASSERT_TRUE(packetReceived >> isSet);
                // It is not acceptable to have an incompletely configured seat
                OD_ASSERT_TRUE(isSet);
                OD_ASSERT_TRUE(packetReceived >> playerId);
                switch(playerId)
                {
                    case 0:
                    {
                        // It is an inactive player
                        break;
                    }
                    case 1:
                    {
                        // It is an AI
                        // We set player id = 0 for AI players. ID is only used during seat configuration phase
                        // During the game, one should use the seat ID to identify a player
                        Player* aiPlayer = new Player(gameMap, 0);
                        aiPlayer->setNick("Keeper AI " + Ogre::StringConverter::toString(seatId));
                        gameMap->addPlayer(aiPlayer);
                        seat->setPlayer(aiPlayer);
                        gameMap->assignAI(*aiPlayer, "KeeperAI");
                        break;
                    }
                    default:
                    {
                        // Human player
                        for (ODSocketClient* client : mSockClients)
                        {
                            if((client->getState().compare("ready") == 0) &&
                               (client->getPlayer()->getId() == playerId))
                            {
                                seat->setPlayer(client->getPlayer());
                                gameMap->addPlayer(client->getPlayer());
                                break;
                            }
                        }
                        break;
                    }
                }

                OD_ASSERT_TRUE(packetReceived >> isSet);
                // It is not acceptable to have an incompletely configured seat
                OD_ASSERT_TRUE(isSet);
                OD_ASSERT_TRUE(packetReceived >> teamId);
                seat->setTeamId(teamId);

                // If a player is assigned to this seat, we create his spawn pool
                if(seat->getPlayer() != nullptr)
                {
                    seat->initSpawnPool();
                    seat->setMapSize(gameMap->getMapSizeX(), gameMap->getMapSizeY());
                }
                else
                    LogManager::getSingleton().logMessage("No spawn pool created for seat id="
                        + Ogre::StringConverter::toString(seat->getId()));
            }

            // Now, we can disconnect the players that were not configured
            std::vector<ODSocketClient*> clientsToRemove;
            for (ODSocketClient* client : mSockClients)
            {
                if(client->getPlayer()->getSeat() == nullptr)
                    clientsToRemove.push_back(client);
            }

            if(!clientsToRemove.empty())
            {
                ODPacket packetSend;
                packetSend << ServerNotification::clientRejected;
                for(ODSocketClient* client : clientsToRemove)
                {
                    Player* player = client->getPlayer();
                    LogManager::getSingleton().logMessage("Rejecting player id="
                        + Ogre::StringConverter::toString(player->getId())
                        + ", nick=" + player->getNick());
                    setClientState(client, "rejected");
                    sendMsgToClient(client, packetSend);
                    delete player;
                    client->setPlayer(nullptr);
                }
            }

            ODPacket packetSend;
            packetSend << ServerNotification::clientAccepted << ODApplication::turnsPerSecond;
            const std::vector<Player*>& players = gameMap->getPlayers();
            int32_t nbPlayers = players.size();
            packetSend << nbPlayers;
            for (Player* player : players)
            {
                packetSend << player->getNick() << player->getId() << player->getSeat()->getId();
            }
            sendMsg(nullptr, packetSend);

            for (ODSocketClient* client : mSockClients)
            {
                if(!client->isConnected() || (client->getPlayer() == nullptr))
                    continue;

                ODPacket packetSend;
                int seatId = client->getPlayer()->getSeat()->getId();
                packetSend << ServerNotification::startGameMode << seatId << mServerMode;
                sendMsgToClient(client, packetSend);
            }

            mSeatsConfigured = true;
            break;
        }

        case ClientNotification::chat:
        {
            // TODO : handle chat for everybody/allies/player
            // As chat message do not interfere with GameMap, it is OK to send
            // them directly to the clients instead of queuing a ServerNotification
            // to the Server
            std::string chatMsg;
            OD_ASSERT_TRUE(packetReceived >> chatMsg);
            Player* player = clientSocket->getPlayer();
            ODPacket packetSend;
            std::string nick = player->getNick();
            ServerNotification notif(ServerNotification::chat, nullptr);
            notif.mPacket << nick << chatMsg;
            sendAsyncMsg(notif);
            break;
        }

        case ClientNotification::askEntityPickUp:
        {
            std::string entityName;
            GameEntity::ObjectType entityType;
            OD_ASSERT_TRUE(packetReceived >> entityType >> entityName);

            Player *player = clientSocket->getPlayer();
            MovableGameEntity* entity = gameMap->getEntityFromTypeAndName(entityType, entityName);
            OD_ASSERT_TRUE_MSG(entity != nullptr, "entityType=" + Ogre::StringConverter::toString(static_cast<int32_t>(entityType)) + ", entityName=" + entityName);
            if(entity == nullptr)
                break;
            bool allowPickup = entity->tryPickup(player->getSeat(), mServerMode == ServerMode::ModeEditor);
            if(!allowPickup)
            {
                LogManager::getSingleton().logMessage("player=" + player->getNick()
                        + " could not pickup entity entityType="
                        + Ogre::StringConverter::toString(static_cast<int32_t>(entityType))
                        + ", entityName=" + entityName);
                break;
            }

            player->pickUpEntity(entity, mServerMode == ServerMode::ModeEditor);
            break;
        }

        case ClientNotification::askHandDrop:
        {
            Player *player = clientSocket->getPlayer();
            Tile* tile = gameMap->tileFromPacket(packetReceived);
            OD_ASSERT_TRUE(tile != nullptr);
            if(tile != nullptr)
            {
                if(player->isDropHandPossible(tile, 0, mServerMode == ServerMode::ModeEditor))
                {
                    // We notify the players
                    OD_ASSERT_TRUE(player->dropHand(tile, 0) != NULL);
                }
                else
                {
                    LogManager::getSingleton().logMessage("player=" + player->getNick()
                        + " could not drop entity in hand on tile "
                        + Tile::displayAsString(tile));
                }
            }
            break;
        }

        case ClientNotification::askPickupWorker:
        {
            Player *player = clientSocket->getPlayer();
            Creature* creature = gameMap->getWorkerToPickupBySeat(player->getSeat());
            if(creature == nullptr)
                break;

            player->pickUpEntity(creature, mServerMode == ServerMode::ModeEditor);
            break;
        }

        case ClientNotification::askPickupFighter:
        {
            Player *player = clientSocket->getPlayer();
            Creature* creature = gameMap->getFighterToPickupBySeat(player->getSeat());
            if(creature == nullptr)
                break;

            player->pickUpEntity(creature, mServerMode == ServerMode::ModeEditor);
            break;
        }

        case ClientNotification::askMarkTile:
        {
            int x1, y1, x2, y2;
            bool isDigSet;
            Player* player = clientSocket->getPlayer();

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> isDigSet);
            std::vector<Tile*> tiles = gameMap->getDiggableTilesForPlayerInArea(x1,
                y1, x2, y2, player);
            if(!tiles.empty())
            {
                // We update the server game map
                for(Tile* tile : tiles)
                    tile->setMarkedForDigging(isDigSet, player);
            }
            // Note that we don't send anything to the player. Displaying the marked tiles
            // is done by the client itself
            break;
        }

        case ClientNotification::askSlapEntity:
        {
            GameEntity::ObjectType entityType;
            std::string entityName;
            Player* player = clientSocket->getPlayer();
            OD_ASSERT_TRUE(packetReceived >> entityType >> entityName);
            MovableGameEntity* entity = gameMap->getEntityFromTypeAndName(entityType, entityName);
            OD_ASSERT_TRUE_MSG(entity != nullptr, "entityType=" + Ogre::StringConverter::toString(static_cast<int32_t>(entityType)) + ", entityName=" + entityName);
            if(entity == nullptr)
                break;


            bool isEditorMode = (mServerMode == ServerMode::ModeEditor);
            if(!entity->canSlap(player->getSeat(), isEditorMode))
            {
                LogManager::getSingleton().logMessage("player=" + player->getNick()
                        + " could not slap entity entityType="
                        + Ogre::StringConverter::toString(static_cast<int32_t>(entityType))
                        + ", entityName=" + entityName);
                break;
            }

            entity->slap(isEditorMode);

            ODPacket packet;
            packet << ServerNotification::entitySlapped;
            sendMsgToClient(clientSocket, packet);
            break;
        }

        case ClientNotification::askBuildRoom:
        {
            int x1, y1, x2, y2;
            Room::RoomType type;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> type);
            Player* player = clientSocket->getPlayer();

            std::vector<Tile*> tiles;
            int goldRequired;
            gameMap->fillBuildableTilesAndPriceForPlayerInArea(x1, y1, x2, y2, player, type, tiles, goldRequired);
            if(tiles.empty())
                break;

            if(gameMap->withdrawFromTreasuries(goldRequired, player->getSeat()) == false)
                break;

            Room* room = nullptr;
            switch (type)
            {
                case Room::RoomType::nullRoomType:
                {
                    room = nullptr;
                    break;
                }
                case Room::RoomType::dormitory:
                {
                    room = new RoomDormitory(gameMap);
                    break;
                }
                case Room::RoomType::dungeonTemple:
                {
                    room = new RoomDungeonTemple(gameMap);
                    break;
                }
                case Room::RoomType::forge:
                {
                    room = new RoomForge(gameMap);
                    break;
                }
                case Room::RoomType::hatchery:
                {
                    room = new RoomHatchery(gameMap);
                    break;
                }
                case Room::RoomType::library:
                {
                    room = new RoomLibrary(gameMap);
                    break;
                }
                case Room::RoomType::portal:
                {
                    room = new RoomPortal(gameMap);
                    break;
                }
                case Room::RoomType::trainingHall:
                {
                    room = new RoomTrainingHall(gameMap);
                    break;
                }
                case Room::RoomType::treasury:
                {
                    room = new RoomTreasury(gameMap);
                    break;
                }
                case Room::RoomType::crypt:
                {
                    room = new RoomCrypt(gameMap);
                    break;
                }
                default:
                    OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Ogre::StringConverter::toString(
                        static_cast<int>(type)));
            }
            if(room == nullptr)
                break;

            room->setupRoom(gameMap->nextUniqueNameRoom(room->getMeshName()), player->getSeat(), tiles);
            gameMap->addRoom(room);
            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since they could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : tiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            room->createMesh();
            room->checkForRoomAbsorbtion();
            room->updateActiveSpots();
            break;
        }

        case ClientNotification::askSellRoomTiles:
        {
            int x1, y1, x2, y2;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2);
            Player* player = clientSocket->getPlayer();
            std::vector<Tile*> tiles = gameMap->rectangularRegion(x1, y1, x2, y2);

            if(tiles.empty())
                break;

            int goldRetrieved = 0;
            std::set<Room*> roomsImpacted;
            std::vector<Tile*> sentTiles;
            for(Tile* tile : tiles)
            {
                if(tile->getCoveringRoom() == nullptr)
                    continue;

                Room* room = tile->getCoveringRoom();
                if(!room->getSeat()->canRoomBeDestroyedBy(player->getSeat()))
                    continue;

                roomsImpacted.insert(room);
                sentTiles.push_back(tile);
                goldRetrieved += Room::costPerTile(room->getType()) / 2;
                OD_ASSERT_TRUE(room->removeCoveredTile(tile));
            }

            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since the could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : sentTiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            // We update active spots of each impacted rooms
            for(Room* room : roomsImpacted)
                room->updateActiveSpots();

            // Note : no need to handle the free treasury tile because if there is no treasury tile, gold will be 0 anyway
            gameMap->addGoldToSeat(goldRetrieved, player->getSeat()->getId());
            break;
        }

        case ClientNotification::editorAskDestroyRoomTiles:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            int x1, y1, x2, y2;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2);
            std::vector<Tile*> tiles = gameMap->rectangularRegion(x1, y1, x2, y2);

            if(tiles.empty())
                break;

            std::set<Room*> roomsImpacted;
            std::vector<Tile*> sentTiles;
            for(Tile* tile : tiles)
            {
                if(tile->getCoveringRoom() == nullptr)
                    continue;

                Room* room = tile->getCoveringRoom();
                roomsImpacted.insert(room);
                sentTiles.push_back(tile);
                OD_ASSERT_TRUE(room->removeCoveredTile(tile));
            }

            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since the could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : sentTiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            // We update active spots of each impacted rooms
            for(Room* room : roomsImpacted)
                room->updateActiveSpots();

            break;
        }

        case ClientNotification::askBuildTrap:
        {
            int x1, y1, x2, y2;
            Trap::TrapType type;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> type);
            Player* player = clientSocket->getPlayer();
            std::vector<Tile*> tiles = gameMap->getBuildableTilesForPlayerInArea(x1,
                y1, x2, y2, player);

            if(tiles.empty())
                break;

            int goldRequired = tiles.size() * Trap::costPerTile(type);
            if(!gameMap->withdrawFromTreasuries(goldRequired, player->getSeat()))
                break;

            Trap* trap = nullptr;
            switch (type)
            {
                case Trap::TrapType::nullTrapType:
                {
                    trap = nullptr;
                    break;
                }
                case Trap::TrapType::cannon:
                {
                    trap = new TrapCannon(gameMap);
                    break;
                }
                case Trap::TrapType::spike:
                {
                    trap = new TrapSpike(gameMap);
                    break;
                }
                case Trap::TrapType::boulder:
                {
                    trap = new TrapBoulder(gameMap);
                    break;
                }
                default:
                    OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Ogre::StringConverter::toString(
                        static_cast<int>(type)));
            }
            if(trap == nullptr)
                break;

            trap->setupTrap(gameMap->nextUniqueNameTrap(trap->getMeshName()), player->getSeat(), tiles);
            gameMap->addTrap(trap);
            trap->createMesh();
            trap->updateActiveSpots();
            break;
        }

        case ClientNotification::askSellTrapTiles:
        {
            int x1, y1, x2, y2;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2);
            Player* player = clientSocket->getPlayer();
            std::vector<Tile*> tiles = gameMap->rectangularRegion(x1, y1, x2, y2);

            if(tiles.empty())
                break;

            int goldRetrieved = 0;
            std::set<Trap*> trapsImpacted;
            std::vector<Tile*> sentTiles;
            for(Tile* tile : tiles)
            {
                if(tile->getCoveringTrap() == nullptr)
                    continue;

                Trap* trap = tile->getCoveringTrap();
                if(!trap->getSeat()->canTrapBeDestroyedBy(player->getSeat()))
                    continue;

                trapsImpacted.insert(trap);
                sentTiles.push_back(tile);
                goldRetrieved += Trap::costPerTile(trap->getType()) / 2;
                OD_ASSERT_TRUE(trap->removeCoveredTile(tile));
            }

            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since the could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : sentTiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            // We update active spots of each impacted rooms
            for(Trap* trap : trapsImpacted)
                trap->updateActiveSpots();

            gameMap->addGoldToSeat(goldRetrieved, player->getSeat()->getId());
            break;
        }

        case ClientNotification::editorAskDestroyTrapTiles:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            int x1, y1, x2, y2;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2);
            std::vector<Tile*> tiles = gameMap->rectangularRegion(x1, y1, x2, y2);

            if(tiles.empty())
                break;

            std::set<Trap*> trapsImpacted;
            std::vector<Tile*> sentTiles;
            for(Tile* tile : tiles)
            {
                if(tile->getCoveringTrap() == nullptr)
                    continue;

                Trap* trap = tile->getCoveringTrap();
                trapsImpacted.insert(trap);
                sentTiles.push_back(tile);
                OD_ASSERT_TRUE(trap->removeCoveredTile(tile));
            }

            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since the could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : sentTiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            // We update active spots of each impacted rooms
            for(Trap* trap : trapsImpacted)
                trap->updateActiveSpots();

            break;
        }

        case ClientNotification::ackNewTurn:
        {
            int64_t turn;
            OD_ASSERT_TRUE(packetReceived >> turn);
            clientSocket->setLastTurnAck(turn);
            break;
        }

        case ClientNotification::askCreatureInfos:
        {
            std::string name;
            bool refreshEachTurn;
            OD_ASSERT_TRUE(packetReceived >> name >> refreshEachTurn);
            std::vector<std::string>& creatures = mCreaturesInfoWanted[clientSocket];

            std::vector<std::string>::iterator it = std::find(creatures.begin(), creatures.end(), name);
            if(refreshEachTurn && (it == creatures.end()))
            {
                creatures.push_back(name);
            }
            else if(!refreshEachTurn && (it != creatures.end()))
                creatures.erase(it);

            break;
        }

        case ClientNotification::editorAskSaveMap:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            Player* player = clientSocket->getPlayer();
            if(player->numObjectsInHand() == 0)
            {
                // If the file exists, we make a backup
                const boost::filesystem::path levelPath(gameMap->getLevelFileName());
                if (boost::filesystem::exists(levelPath))
                    boost::filesystem::rename(gameMap->getLevelFileName(), gameMap->getLevelFileName() + ".bak");

                std::string msg = "Map saved successfully";
                MapLoader::writeGameMapToFile(gameMap->getLevelFileName(), *gameMap);
                ServerNotification notif(ServerNotification::chatServer, player);
                notif.mPacket << msg;
                sendAsyncMsg(notif);
            }
            else
            {
                // We cannot save the map
                std::string msg = "Map could not be saved because player hand is not empty";
                ServerNotification notif(ServerNotification::chatServer, player);
                notif.mPacket << msg;
                sendAsyncMsg(notif);
            }
            break;
        }

        case ClientNotification::editorAskChangeTiles:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            int x1, y1, x2, y2;
            int intTileType;
            double tileFullness;
            int seatId;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> intTileType >> tileFullness >> seatId);
            Tile::TileType tileType = static_cast<Tile::TileType>(intTileType);
            std::vector<Tile*> selectedTiles = gameMap->rectangularRegion(x1, y1, x2, y2);
            std::vector<Tile*> affectedTiles;
            Seat* seat = nullptr;
            double claimedPercentage = 0.0;
            if(tileType == Tile::TileType::claimed)
            {
                seat = gameMap->getSeatById(seatId);
                claimedPercentage = 1.0;
            }
            for(Tile* tile : selectedTiles)
            {
                // We do not change tiles where there is something
                if((tile->numEntitiesInTile() > 0) &&
                   ((tileFullness > 0.0) || (tileType == Tile::TileType::lava) || (tileType == Tile::TileType::water)))
                    continue;
                if(tile->getCoveringBuilding() != nullptr)
                    continue;

                affectedTiles.push_back(tile);
                tile->setType(tileType);
                tile->setFullness(tileFullness);
                tile->setSeat(seat);
                tile->mClaimedPercentage = claimedPercentage;
            }
            if(!affectedTiles.empty())
            {
                uint32_t nbTiles = affectedTiles.size();
                const std::vector<Seat*>& seats = gameMap->getSeats();
                for(Seat* seat : seats)
                {
                    if(seat->getPlayer() == nullptr)
                        continue;
                    if(!seat->getPlayer()->getIsHuman())
                        continue;

                    ServerNotification notif(ServerNotification::refreshTiles, seat->getPlayer());
                    notif.mPacket << nbTiles;
                    for(Tile* tile : affectedTiles)
                    {
                        gameMap->tileToPacket(notif.mPacket, tile);
                        tile->exportToPacket(notif.mPacket, seat);
                    }
                    sendAsyncMsg(notif);
                }
            }
            break;
        }

        case ClientNotification::editorAskBuildRoom:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            int x1, y1, x2, y2;
            int seatId;
            Room::RoomType type;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> type >> seatId);
            Seat* seat = gameMap->getSeatById(seatId);
            OD_ASSERT_TRUE_MSG(seat != NULL, "seatId=" + Ogre::StringConverter::toString(seatId));
            std::vector<Tile*> selectedTiles = gameMap->rectangularRegion(x1, y1, x2, y2);
            std::vector<Tile*> tiles;
            // We start by changing the tiles so that the room can be built
            for(Tile* tile : selectedTiles)
            {
                // We do not change tiles where there is something on the tile
                if(tile->getCoveringBuilding() != nullptr)
                    continue;
                tiles.push_back(tile);

                tile->setType(Tile::TileType::claimed);
                tile->setSeat(seat);
                tile->setFullness(0.0);
            }

            if(tiles.empty())
                break;

            Room* room = nullptr;
            switch (type)
            {
                case Room::RoomType::nullRoomType:
                {
                    room = nullptr;
                    break;
                }
                case Room::RoomType::dormitory:
                {
                    room = new RoomDormitory(gameMap);
                    break;
                }
                case Room::RoomType::dungeonTemple:
                {
                    room = new RoomDungeonTemple(gameMap);
                    break;
                }
                case Room::RoomType::forge:
                {
                    room = new RoomForge(gameMap);
                    break;
                }
                case Room::RoomType::hatchery:
                {
                    room = new RoomHatchery(gameMap);
                    break;
                }
                case Room::RoomType::library:
                {
                    room = new RoomLibrary(gameMap);
                    break;
                }
                case Room::RoomType::portal:
                {
                    room = new RoomPortal(gameMap);
                    break;
                }
                case Room::RoomType::trainingHall:
                {
                    room = new RoomTrainingHall(gameMap);
                    break;
                }
                case Room::RoomType::treasury:
                {
                    room = new RoomTreasury(gameMap);
                    break;
                }
                case Room::RoomType::crypt:
                {
                    room = new RoomCrypt(gameMap);
                    break;
                }
                default:
                    OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Ogre::StringConverter::toString(
                        static_cast<int>(type)));
            }
            if(room == nullptr)
                break;

            room->setupRoom(gameMap->nextUniqueNameRoom(room->getMeshName()), seat, tiles);

            gameMap->addRoom(room);

            // We notify the clients with vision of the changed tiles. Note that we need
            // to calculate per seat since the could have vision on different parts of the building
            std::map<Seat*,std::vector<Tile*>> tilesPerSeat;
            const std::vector<Seat*>& seats = gameMap->getSeats();
            for(Seat* seat : seats)
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                for(Tile* tile : tiles)
                {
                    if(!seat->hasVisionOnTile(tile))
                        continue;

                    tile->changeNotifiedForSeat(seat);
                    tilesPerSeat[seat].push_back(tile);
                }
            }

            for(const std::pair<Seat* const,std::vector<Tile*>>& p : tilesPerSeat)
            {
                uint32_t nbTiles = p.second.size();
                ServerNotification serverNotification(
                    ServerNotification::refreshTiles, p.first->getPlayer());
                serverNotification.mPacket << nbTiles;
                for(Tile* tile : p.second)
                {
                    gameMap->tileToPacket(serverNotification.mPacket, tile);
                    tile->exportToPacket(serverNotification.mPacket, p.first);
                }
                sendAsyncMsg(serverNotification);
            }

            room->createMesh();
            room->checkForRoomAbsorbtion();
            room->updateActiveSpots();
            break;
        }

        case ClientNotification::editorAskBuildTrap:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            int x1, y1, x2, y2;
            int seatId;
            Trap::TrapType type;

            OD_ASSERT_TRUE(packetReceived >> x1 >> y1 >> x2 >> y2 >> type >> seatId);
            Seat* seat = gameMap->getSeatById(seatId);
            OD_ASSERT_TRUE_MSG(seat != NULL, "seatId=" + Ogre::StringConverter::toString(seatId));
            std::vector<Tile*> selectedTiles = gameMap->rectangularRegion(x1, y1, x2, y2);
            std::vector<Tile*> tiles;
            // We start by changing the tiles so that the trap can be built
            for(Tile* tile : selectedTiles)
            {
                // We do not change tiles where there is something
                if(tile->getCoveringBuilding() != nullptr)
                    continue;
                tiles.push_back(tile);

                tile->setType(Tile::TileType::claimed);
                tile->setSeat(seat);
                tile->setFullness(0.0);
            }

            if(tiles.empty())
                break;

            Trap* trap = nullptr;
            switch (type)
            {
                case Trap::TrapType::nullTrapType:
                {
                    trap = nullptr;
                    break;
                }
                case Trap::TrapType::cannon:
                {
                    trap = new TrapCannon(gameMap);
                    break;
                }
                case Trap::TrapType::spike:
                {
                    trap = new TrapSpike(gameMap);
                    break;
                }
                case Trap::TrapType::boulder:
                {
                    trap = new TrapBoulder(gameMap);
                    break;
                }
                default:
                    OD_ASSERT_TRUE_MSG(false, "Unknown enum value : " + Ogre::StringConverter::toString(
                        static_cast<int>(type)));
            }

            if(trap == nullptr)
                break;

            trap->setupTrap(gameMap->nextUniqueNameTrap(trap->getMeshName()), seat, tiles);
            gameMap->addTrap(trap);
            trap->createMesh();
            trap->updateActiveSpots();
            break;
        }

        case ClientNotification::editorCreateWorker:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            Player* player = clientSocket->getPlayer();
            int seatId;
            OD_ASSERT_TRUE(packetReceived >> seatId);
            Seat* seatCreature = gameMap->getSeatById(seatId);
            OD_ASSERT_TRUE_MSG(seatCreature != NULL, "seatId=" + Ogre::StringConverter::toString(seatId));
            const CreatureDefinition *classToSpawn = gameMap->getClassDescription("Kobold");
            OD_ASSERT_TRUE(classToSpawn != nullptr);
            if(classToSpawn == nullptr)
                break;
            Creature* newCreature = new Creature(gameMap, classToSpawn);
            newCreature->setSeat(seatCreature);
            gameMap->addCreature(newCreature);
            newCreature->createMesh();
            // In editor mode, every player has vision
            for(Seat* seat : gameMap->getSeats())
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                newCreature->addSeatWithVision(seat, true);
            }

            player->pickUpEntity(newCreature, mServerMode == ServerMode::ModeEditor);
            break;
        }

        case ClientNotification::editorCreateFighter:
        {
            OD_ASSERT_TRUE_MSG(mServerMode == ServerMode::ModeEditor, "Received editor command while wrong mode mode"
                + Ogre::StringConverter::toString(static_cast<int>(mServerMode)));
            Player* player = clientSocket->getPlayer();
            int seatId;
            std::string className;
            OD_ASSERT_TRUE(packetReceived >> seatId >> className);
            Seat* seatCreature = gameMap->getSeatById(seatId);
            OD_ASSERT_TRUE_MSG(seatCreature != NULL, "seatId=" + Ogre::StringConverter::toString(seatId));
            const CreatureDefinition *classToSpawn = gameMap->getClassDescription(className);
            OD_ASSERT_TRUE_MSG(classToSpawn != nullptr, "Couldn't spawn creature class=" + className);
            if(classToSpawn == nullptr)
                break;
            Creature* newCreature = new Creature(gameMap, classToSpawn);
            newCreature->setSeat(seatCreature);
            gameMap->addCreature(newCreature);
            newCreature->createMesh();
            // In editor mode, every player has vision
            for(Seat* seat : gameMap->getSeats())
            {
                if(seat->getPlayer() == nullptr)
                    continue;
                if(!seat->getPlayer()->getIsHuman())
                    continue;

                newCreature->addSeatWithVision(seat, true);
            }

            player->pickUpEntity(newCreature, mServerMode == ServerMode::ModeEditor);
            break;
        }

        default:
        {
            LogManager::getSingleton().logMessage("ERROR:  Unhandled command received from client:"
                + Ogre::StringConverter::toString(clientCommand));
        }
    }

    return true;
}

bool ODServer::notifyNewConnection(ODSocketClient *clientSocket)
{
    GameMap* gameMap = mGameMap;
    if (gameMap == NULL)
        return false;

    switch(mServerState)
    {
        case ServerState::StateNone:
        {
            // It is not normal to receive new connexions while not connected. We are in an unexpected state
            OD_ASSERT_TRUE(false);
            return false;
        }
        case ServerState::StateConfiguration:
        {
            setClientState(clientSocket, "connected");
            return true;
        }
        case ServerState::StateGame:
        {
            // TODO : handle re-connexion if a client was disconnected and tries to reconnect
            return false;
        }
    }

    return false;
}

bool ODServer::notifyClientMessage(ODSocketClient *clientSocket)
{
    bool ret = processClientNotifications(clientSocket);
    if(!ret)
    {
        LogManager::getSingleton().logMessage("Client disconnected state=" + clientSocket->getState());
        if(std::string("ready").compare(clientSocket->getState()) == 0)
        {
            ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::chatServer, nullptr);
            std::string msg = clientSocket->getPlayer()->getNick()
                + " disconnected";
            serverNotification->mPacket << msg;
            queueServerNotification(serverNotification);
        }
        // TODO : wait at least 1 minute if the client reconnects if deconnexion happens during game
    }
    return ret;
}

void ODServer::stopServer()
{
    // We start by stopping server to make sure no new message comes
    ODSocketServer::stopServer();

    // Now that the server is stopped, we can remove all pending messages
    while(!mServerNotificationQueue.empty())
    {
        delete mServerNotificationQueue.front();
        mServerNotificationQueue.pop_front();
    }
    mGameMap->clearAll();
    mGameMap->processDeletionQueues();
}

void ODServer::notifyExit()
{
    ODSocketServer::stopServer();
    while(!mServerNotificationQueue.empty())
    {
        delete mServerNotificationQueue.front();
        mServerNotificationQueue.pop_front();
    }

    ServerNotification* exitServerNotification = new ServerNotification(
        ServerNotification::exit, nullptr);
    queueServerNotification(exitServerNotification);
}

ODSocketClient* ODServer::getClientFromPlayer(Player* player)
{
    ODSocketClient* ret = NULL;

    for (ODSocketClient* client : mSockClients)
    {
        if(client->getPlayer() == player)
        {
            ret = client;
            break;
        }
    }

    return ret;
}

ODPacket& operator<<(ODPacket& os, const ODServer::ServerMode& sm)
{
    os << static_cast<int32_t>(sm);
    return os;
}

ODPacket& operator>>(ODPacket& is, ODServer::ServerMode& sm)
{
    int32_t tmp;
    is >> tmp;
    sm = static_cast<ODServer::ServerMode>(tmp);
    return is;
}
