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

#include "creaturebehaviour/CreatureBehaviourFleeWhenWeak.h"

#include "creaturebehaviour/CreatureBehaviourManager.h"
#include "entities/Creature.h"
#include "entities/CreatureDefinition.h"
#include "utils/Random.h"

const std::string CreatureBehaviourFleeWhenWeak::mNameCreatureBehaviourFleeWhenWeak = "FleeWhenWeak";

namespace
{
class CreatureBehaviourFleeWhenWeakFactory : public CreatureBehaviourFactory
{
    CreatureBehaviour* createCreatureBehaviour() const override
    { return new CreatureBehaviourFleeWhenWeak; }

    const std::string& getCreatureBehaviourName() const override
    {
        return CreatureBehaviourFleeWhenWeak::mNameCreatureBehaviourFleeWhenWeak;
    }
};

// Register the factory
static CreatureBehaviourRegister reg(new CreatureBehaviourFleeWhenWeakFactory);
}

CreatureBehaviourFleeWhenWeak::CreatureBehaviourFleeWhenWeak(const CreatureBehaviourFleeWhenWeak& behaviour) :
    CreatureBehaviour(),
    mWeakCoef(behaviour.mWeakCoef)
{
}

CreatureBehaviour* CreatureBehaviourFleeWhenWeak::clone() const
{
    return new CreatureBehaviourFleeWhenWeak(*this);
}

bool CreatureBehaviourFleeWhenWeak::processBehaviour(Creature& creature) const
{
    if(creature.getHP() > creature.getMaxHp() * mWeakCoef)
        return true;

    if(!creature.getVisibleEnemyObjects().empty())
    {
        // If there is an enemy, we should flee
        if(creature.isActionInList(CreatureActionType::flee))
            return true;

        creature.clearDestinations(EntityAnimation::idle_anim, true);
        creature.clearActionQueue();
        creature.pushAction(CreatureActionType::flee, false, true);
        return true;
    }

    // We randomly choose to flee
    if(Random::Uint(0, 100) < 20)
    {
        if(creature.isActionInList(CreatureActionType::flee))
            return true;

        creature.clearDestinations(EntityAnimation::idle_anim, true);
        creature.clearActionQueue();
        creature.pushAction(CreatureActionType::flee, false, true);
        return true;
    }

    return true;
}

void CreatureBehaviourFleeWhenWeak::getFormatString(std::string& format) const
{
    if(!format.empty())
        format += "\t";

    format += "WeakCoef";
}

bool CreatureBehaviourFleeWhenWeak::isEqual(const CreatureBehaviour& creatureBehaviour) const
{
    if(!CreatureBehaviour::isEqual(creatureBehaviour))
        return false;

    const CreatureBehaviourFleeWhenWeak* cb = dynamic_cast<const CreatureBehaviourFleeWhenWeak*>(&creatureBehaviour);
    if(cb == nullptr)
        return false;

    if(mWeakCoef != cb->mWeakCoef)
        return false;

    return true;
}

void CreatureBehaviourFleeWhenWeak::exportToStream(std::ostream& os) const
{
    CreatureBehaviour::exportToStream(os);
    os << "\t" << mWeakCoef;
}

bool CreatureBehaviourFleeWhenWeak::importFromStream(std::istream& is)
{
    if(!CreatureBehaviour::importFromStream(is))
        return false;

    if(!(is >> mWeakCoef))
        return false;

    return true;
}
