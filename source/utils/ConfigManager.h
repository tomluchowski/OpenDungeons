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

#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <OgreSingleton.h>
#include <OgreColourValue.h>

#include <cstdint>

class CreatureDefinition;
class Weapon;
class SpawnCondition;

//! \brief This class is used to manage global configuration such as network configuration, global creature stats, ...
//! It should NOT be used to load level specific stuff. For that, there if GameMap.
class ConfigManager : public Ogre::Singleton<ConfigManager>
{
public:
    ConfigManager();
    ~ConfigManager();

    Ogre::ColourValue getColorFromId(const std::string& id) const;
    const std::vector<const CreatureDefinition*>& getCreatureDefinitions() const
    { return mCreatureDefs; }
    const CreatureDefinition* getCreatureDefinition(const std::string& name) const;

    const std::vector<const Weapon*>& getWeapons() const
    { return mWeapons; }
    const Weapon* getWeapon(const std::string& name) const;

    uint32_t getCreatureDeathCounter() const
    { return mCreatureDeathCounter; }

    uint32_t getMaxCreaturesPerSeat() const
    { return mMaxCreaturesPerSeat; }

    double getSlapDamagePercent() const
    { return mSlapDamagePercent; }

    uint32_t getNetworkPort() const
    { return mNetworkPort; }

    uint32_t getBaseSpawnPoint() const
    { return mBaseSpawnPoint; }

    const std::vector<const SpawnCondition*>& getCreatureSpawnConditions(const CreatureDefinition* def) const;

    const std::vector<std::string>& getFactionSpawnPool(const std::string& faction) const;

    const std::vector<std::string>& getFactions() const
    { return mFactions; }

    //! Rooms configuration
    const std::string& getRoomConfigString(const std::string& param) const;
    uint32_t getRoomConfigUInt32(const std::string& param) const;
    int32_t getRoomConfigInt32(const std::string& param) const;
    double getRoomConfigDouble(const std::string& param) const;

    //! Traps configuration
    const std::string& getTrapConfigString(const std::string& param) const;
    uint32_t getTrapConfigUInt32(const std::string& param) const;
    int32_t getTrapConfigInt32(const std::string& param) const;
    double getTrapConfigDouble(const std::string& param) const;

private:
    //! \brief Function used to load the global configuration. They should return true if the configuration
    //! is ok and false if a mandatory parameter is missing
    bool loadGlobalConfig();
    bool loadGlobalConfigSeatColors(std::stringstream& configFile);
    bool loadGlobalConfigDefinitionFiles(std::stringstream& configFile);
    bool loadGlobalGameConfig(std::stringstream& configFile);
    bool loadCreatureDefinitions(const std::string& fileName);
    bool loadEquipements(const std::string& fileName);
    bool loadSpawnConditions(const std::string& fileName);
    bool loadFactions(const std::string& fileName);
    bool loadRooms(const std::string& fileName);
    bool loadTraps(const std::string& fileName);

    std::map<std::string, Ogre::ColourValue> mSeatColors;
    std::vector<const CreatureDefinition*> mCreatureDefs;
    std::vector<const Weapon*> mWeapons;
    std::string mFilenameCreatureDefinition;
    std::string mFilenameEquipmentDefinition;
    std::string mFilenameSpawnConditions;
    std::string mFilenameFactions;
    std::string mFilenameRooms;
    std::string mFilenameTraps;
    uint32_t mNetworkPort;
    uint32_t mBaseSpawnPoint;
    uint32_t mCreatureDeathCounter;
    uint32_t mMaxCreaturesPerSeat;
    double mSlapDamagePercent;
    std::map<const CreatureDefinition*, std::vector<const SpawnCondition*> > mCreatureSpawnConditions;
    std::map<const std::string, std::vector<std::string> > mFactionSpawnPool;
    std::vector<std::string> mFactions;
    std::map<const std::string, std::string> mRoomsConfig;
    std::map<const std::string, std::string> mTrapsConfig;
};

#endif //CONFIGMANAGER_H
