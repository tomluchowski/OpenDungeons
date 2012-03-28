#ifndef ROOM_H
#define ROOM_H

#include <Ogre.h>
#include <string>
#include <deque>
#include <iostream>

#include "Building.h"
#include "Tile.h"

class Seat;
class RoomObject;
class GameMap;

class Room : public Building
{
    public:
        // When room types are added to this enum they also need to be added to the switch statements in Room.cpp.
        enum RoomType
        {
            nullRoomType = 0,
            dungeonTemple,
            quarters,
            treasury,
            portal,
            forge,
            dojo
        };

        // Constructors and operators
        Room();
        virtual ~Room() {}

        static Room* createRoom(RoomType nType,
                const std::vector<Tile*> &nCoveredTiles, int nColor);
        static Room* createRoomFromStream(std::istream &is, GameMap* gameMap);
        static Room* buildRoom(GameMap* gameMap, RoomType nType,
                const std::vector<Tile*> &coveredTiles, Player* player, bool inEditor = false);
        virtual void absorbRoom(Room *r);

        static std::string getFormat();
        friend std::ostream& operator<<(std::ostream& os, Room *r);
        friend std::istream& operator>>(std::istream& is, Room *r);

        virtual void createMesh();
        virtual void destroyMesh();
        RoomObject* loadRoomObject(std::string meshName, Tile *targetTile =
                NULL, double rotationAngle = 0.0);
        RoomObject* loadRoomObject(std::string meshName, Tile *targetTile,
                double x, double y, double rotationAngle);
        void createRoomObjectMeshes();
        void destroyRoomObjectMeshes();
        const RoomType& getType() const{return type;}

        static const char* getMeshNameFromRoomType(RoomType t);
        static RoomType getRoomTypeFromMeshName(const std::string& s);

        static int costPerTile(RoomType t);

        // Functions which can be overridden by child classes.
        virtual bool doUpkeep();
        virtual bool doUpkeep(Room *r);

        virtual void addCoveredTile(Tile* t, double nHP = defaultTileHP);
        virtual void removeCoveredTile(Tile* t);
        virtual Tile* getCoveredTile(int index);
        std::vector<Tile*> getCoveredTiles();
        virtual unsigned int numCoveredTiles();
        virtual void clearCoveredTiles();
        virtual bool tileIsPassable(Tile *t);

        virtual void addCreatureUsingRoom(Creature *c);
        virtual void removeCreatureUsingRoom(Creature *c);
        virtual Creature* getCreatureUsingRoom(int index);
        virtual unsigned int numCreaturesUsingRoom();
        virtual int numOpenCreatureSlots();

        Tile* getCentralTile();
        const Tile* getCentralTile() const;

        // Methods inherited from AttackableObject.
        //TODO:  Sort these into the proper places in the rest of the file.
        double getHP(Tile *tile);
        double getDefense() const;
        void takeDamage(double damage, Tile *tileTakingDamage);
        void recieveExp(double experience);

    protected:
        const static double defaultTileHP;// = 10.0;

        std::vector<Tile*> coveredTiles;
        std::map<Tile*, double> tileHP;
        std::map<Tile*, RoomObject*> roomObjects;
        std::vector<Creature*> creaturesUsingRoom;
        RoomType type;
};

#include "RoomQuarters.h"
#include "RoomTreasury.h"
#include "RoomPortal.h"
#include "RoomDungeonTemple.h"
#include "RoomForge.h"
#include "RoomDojo.h"

#endif

