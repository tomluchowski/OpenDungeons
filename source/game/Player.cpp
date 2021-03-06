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

#include "game/Player.h"

#include "game/Seat.h"

#include "entities/Creature.h"
#include "entities/RenderedMovableEntity.h"

#include "gamemap/GameMap.h"

#include "network/ODServer.h"
#include "network/ServerNotification.h"
#include "network/ODClient.h"

#include "render/RenderManager.h"

#include "utils/LogManager.h"

Player::Player(GameMap* gameMap, int32_t id) :
    mId(id),
    mNewRoomType(Room::nullRoomType),
    mNewTrapType(Trap::nullTrapType),
    mCurrentAction(SelectedAction::none),
    mGameMap(gameMap),
    mSeat(nullptr),
    mIsHuman(false),
    mFightingTime(0.0f),
    mIsPlayerLostSent(false)
{
}

unsigned int Player::numCreaturesInHand(const Seat* seat) const
{
    unsigned int cpt = 0;
    for(MovableGameEntity* entity : mObjectsInHand)
    {
        if(entity->getObjectType() != GameEntity::ObjectType::creature)
            continue;

        if(seat != NULL && entity->getSeat() != seat)
            continue;

        ++cpt;
    }
    return cpt;
}

unsigned int Player::numObjectsInHand() const
{
    return mObjectsInHand.size();
}

void Player::addEntityToHand(MovableGameEntity *entity)
{
    if (mObjectsInHand.empty())
    {
        mObjectsInHand.push_back(entity);
        return;
    }

    // creaturesInHand.push_front(c);
    // Since vectors have no push_front method,
    // we need to move all of the elements in the vector back one
    // and then add this one to the beginning.
    mObjectsInHand.push_back(NULL);
    for (unsigned int j = mObjectsInHand.size() - 1; j > 0; --j)
        mObjectsInHand[j] = mObjectsInHand[j - 1];

    mObjectsInHand[0] = entity;
}

void Player::pickUpEntity(MovableGameEntity *entity, bool isEditorMode)
{
    if (!ODServer::getSingleton().isConnected() && !ODClient::getSingleton().isConnected())
        return;

    if(entity->getObjectType() == GameEntity::ObjectType::creature)
    {
        Creature* creature = static_cast<Creature*>(entity);
        if(!creature->tryPickup(getSeat(), isEditorMode))
           return;

       creature->pickup();
    }
    else if(entity->getObjectType() == GameEntity::ObjectType::renderedMovableEntity)
    {
        RenderedMovableEntity* obj = static_cast<RenderedMovableEntity*>(entity);
        if(!obj->tryPickup(getSeat(), isEditorMode))
           return;

        obj->pickup();
    }

    // Start tracking this creature as being in this player's hand
    addEntityToHand(entity);

    if (mGameMap->isServerGameMap())
    {
        entity->firePickupEntity(this, isEditorMode);
        return;
    }

    OD_ASSERT_TRUE(this == mGameMap->getLocalPlayer());
    if (this == mGameMap->getLocalPlayer())
    {
        // Send a render request to move the crature into the "hand"
        RenderManager::getSingleton().rrPickUpEntity(entity, this);
    }
}

void Player::clearObjectsInHand()
{
    mObjectsInHand.clear();
}

bool Player::isDropHandPossible(Tile *t, unsigned int index, bool isEditorMode)
{
    // if we have a creature to drop
    if (mObjectsInHand.empty())
        return false;

    GameEntity* entity = mObjectsInHand[index];
    if(entity != nullptr && entity->getObjectType() == GameEntity::ObjectType::creature)
    {
        Creature* creature = static_cast<Creature*>(entity);
        if(creature->tryDrop(getSeat(), t, isEditorMode))
            return true;
    }
    else if(entity != nullptr && entity->getObjectType() == GameEntity::ObjectType::renderedMovableEntity)
    {
        RenderedMovableEntity* obj = static_cast<RenderedMovableEntity*>(entity);
        if(obj->tryDrop(getSeat(), t, isEditorMode))
            return true;
    }

    return false;
}

MovableGameEntity* Player::dropHand(Tile *t, unsigned int index)
{
    // Add the creature to the map
    OD_ASSERT_TRUE(index < mObjectsInHand.size());
    if(index >= mObjectsInHand.size())
        return nullptr;

    MovableGameEntity *entity = mObjectsInHand[index];
    mObjectsInHand.erase(mObjectsInHand.begin() + index);
    if(entity->getObjectType() == GameEntity::ObjectType::creature)
    {
        Creature* creature = static_cast<Creature*>(entity);
        creature->drop(Ogre::Vector3(static_cast<Ogre::Real>(t->x),
            static_cast<Ogre::Real>(t->y), 0.0));

        if (!mGameMap->isServerGameMap() && (this == mGameMap->getLocalPlayer()))
        {
            creature->fireCreatureSound(CreatureSound::DROP);
        }
    }
    else if(entity->getObjectType() == GameEntity::ObjectType::renderedMovableEntity)
    {
        RenderedMovableEntity* obj = static_cast<RenderedMovableEntity*>(entity);
        obj->drop(Ogre::Vector3(static_cast<Ogre::Real>(t->x),
            static_cast<Ogre::Real>(t->y), 0.0));
    }

    if(mGameMap->isServerGameMap())
    {
        entity->fireDropEntity(this, t);
        return entity;
    }

    // If this is the result of another player dropping the creature it is currently not visible so we need to create a mesh for it
    //cout << "\nthis:  " << this << "\nme:  " << gameMap->getLocalPlayer() << endl;
    //cout.flush();
    OD_ASSERT_TRUE(this == mGameMap->getLocalPlayer());
    if(this == mGameMap->getLocalPlayer())
    {
        // Send a render request to rearrange the creatures in the hand to move them all forward 1 place
        RenderManager::getSingleton().rrDropHand(entity, this);
    }

    return entity;
}

void Player::rotateHand(int n)
{
    // If there are no creatures or only one creature in our hand, rotation doesn't change the order.
    if (mObjectsInHand.size() <= 1)
        return;

    for (unsigned int i = 0; i < (unsigned int) fabs((double) n); ++i)
    {
        if (n > 0)
        {
            MovableGameEntity* entity = mObjectsInHand.back();

            // Since vectors have no push_front method
            // we need to move all of the elements in the vector back one and then add this one to the beginning.
            for (unsigned int j = mObjectsInHand.size() - 1; j > 0; --j)
                mObjectsInHand[j] = mObjectsInHand[j - 1];

            mObjectsInHand[0] = entity;

        }
        else
        {
            MovableGameEntity* entity = mObjectsInHand.front();
            mObjectsInHand.erase(mObjectsInHand.begin());
            mObjectsInHand.push_back(entity);
        }
    }

    // Send a render request to move the entity into the "hand"
    RenderManager::getSingleton().rrRotateHand(this);
}

void Player::notifyNoMoreDungeonTemple()
{
    if(mIsPlayerLostSent)
        return;

    mIsPlayerLostSent = true;
    // We check if there is still a player in the team with a dungeon temple. If yes, we notify the player he lost his dungeon
    // if no, we notify the team they lost
    std::vector<Room*> dungeonTemples = mGameMap->getRoomsByType(Room::RoomType::dungeonTemple);
    bool hasTeamLost = true;
    for(Room* dungeonTemple : dungeonTemples)
    {
        if(getSeat()->isAlliedSeat(dungeonTemple->getSeat()))
        {
            hasTeamLost = false;
            break;
        }
    }

    if(hasTeamLost)
    {
        // This message will be sent in 1v1 or multiplayer so it should not talk about team. If we want to be
        // more precise, we shall handle the case
        for(Seat* seat : mGameMap->getSeats())
        {
            if(seat->getPlayer() == nullptr)
                continue;
            if(!seat->getPlayer()->getIsHuman())
                continue;
            if(!getSeat()->isAlliedSeat(seat))
                continue;

            ServerNotification *serverNotification = new ServerNotification(
            ServerNotification::chatServer, seat->getPlayer());
            serverNotification->mPacket << "You lost the game";
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
    }
    else
    {
        for(Seat* seat : mGameMap->getSeats())
        {
            if(seat->getPlayer() == nullptr)
                continue;
            if(!seat->getPlayer()->getIsHuman())
                continue;
            if(!getSeat()->isAlliedSeat(seat))
                continue;

            if(this == seat->getPlayer())
            {
                ServerNotification *serverNotification = new ServerNotification(
                ServerNotification::chatServer, seat->getPlayer());
                serverNotification->mPacket << "You lost";
                ODServer::getSingleton().queueServerNotification(serverNotification);
                continue;
            }

            ServerNotification *serverNotification = new ServerNotification(
            ServerNotification::chatServer, seat->getPlayer());
            serverNotification->mPacket << "An ally has lost";
            ODServer::getSingleton().queueServerNotification(serverNotification);
        }
    }
}
