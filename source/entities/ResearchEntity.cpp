/*
 *  Copyright (C) 2011-2015  OpenDungeons Team
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

#include "entities/ResearchEntity.h"

#include "entities/Creature.h"
#include "entities/Tile.h"

#include "network/ODPacket.h"

#include "game/Research.h"

#include "gamemap/GameMap.h"

#include "traps/Trap.h"
#include "traps/TrapBoulder.h"
#include "traps/TrapCannon.h"
#include "traps/TrapSpike.h"

#include "utils/Random.h"
#include "utils/LogManager.h"

#include <iostream>

const std::string EMPTY_STRING;

const Ogre::Vector3 SCALE(0.5,0.5,0.5);

ResearchEntity::ResearchEntity(GameMap* gameMap, bool isOnServerMap, const std::string& libraryName, int32_t researchPoints) :
    RenderedMovableEntity(gameMap, isOnServerMap, libraryName, "Grimoire", 0.0f, false, 1.0f),
    mResearchPoints(researchPoints)
{
    mPrevAnimationState = "Loop";
    mPrevAnimationStateLoop = true;
}

ResearchEntity::ResearchEntity(GameMap* gameMap, bool isOnServerMap) :
    RenderedMovableEntity(gameMap, isOnServerMap)
{
}

const Ogre::Vector3& ResearchEntity::getScale() const
{
    return SCALE;
}

void ResearchEntity::notifyEntityCarryOn(Creature* carrier)
{
    removeEntityFromPositionTile();
    setSeat(carrier->getSeat());
}

void ResearchEntity::notifyEntityCarryOff(const Ogre::Vector3& position)
{
    mPosition = position;
    addEntityToPositionTile();
}

ResearchEntity* ResearchEntity::getResearchEntityFromStream(GameMap* gameMap, std::istream& is)
{
    ResearchEntity* obj = new ResearchEntity(gameMap, true);
    obj->importFromStream(is);
    return obj;
}

ResearchEntity* ResearchEntity::getResearchEntityFromPacket(GameMap* gameMap, ODPacket& is)
{
    ResearchEntity* obj = new ResearchEntity(gameMap, false);
    obj->importFromPacket(is);
    return obj;
}

void ResearchEntity::exportToStream(std::ostream& os) const
{
    RenderedMovableEntity::exportToStream(os);
    os << mResearchPoints << "\t";
    os << mPosition.x << "\t" << mPosition.y << "\t" << mPosition.z << "\t";
}

bool ResearchEntity::importFromStream(std::istream& is)
{
    if(!RenderedMovableEntity::importFromStream(is))
        return false;
    if(!(is >> mResearchPoints))
        return false;
    if(!(is >> mPosition.x >> mPosition.y >> mPosition.z))
        return false;

    return true;
}

std::string ResearchEntity::getResearchEntityStreamFormat()
{
    std::string format = RenderedMovableEntity::getRenderedMovableEntityStreamFormat();
    if(!format.empty())
        format += "\t";

    format += "researchPoints\tPosX\tPosY\tPosZ";

    return format;
}
