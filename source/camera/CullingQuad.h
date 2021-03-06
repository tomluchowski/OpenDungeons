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

#ifndef CULLINGQUAD_H
#define CULLINGQUAD_H

#include "camera/FastPODAllocator.h"
#include "entities/Creature.h"

#include <fstream>
#include <OgreVector2.h>
#include <string>
#include <vector>
#include <stack>
#include <list>
#include <set>

using std::string;
using std::vector;
using std::stack;
using std::list;
using std::istream;
using std::ostream;
using std::set;

typedef enum  node_names {UR,UL,BL,BR,ROOT} NodeNames;
enum side_names{LEFT,RIGHT};
typedef enum quad_part{ALL, SOME,NONE} QuadPart;

struct Entry
{
public:

    Entry(Creature* cc):
        creature_list(1, cc)
    {
        this->index_point = cc->get2dPosition();
    }

    Entry(Creature& cc):
        creature_list(1, &cc)
    {
        this->index_point = cc.get2dPosition();
    }

    bool operator==(Entry& ee)
    {
        return  index_point == ee.index_point;
    }

    void changeQuad(CullingQuad* qq)
    {
        //std::cerr << " changeQuads :: " << qq << std::endl;
        //printList();
        for(std::list<Creature*>::iterator it = creature_list.begin(); it != creature_list.end(); ++it)
        {
            (*it)->setQuad(qq);
        }
        /* std::cerr << "changed!" << std::endl; */
    }

    void printList()
    {
        for(std::list<Creature*>::iterator it =  creature_list.begin(); it != creature_list.end(); ++it)
            std::cerr << (*it)->getName() << ", " << std::endl;
    }

    bool listFind(Creature* cc)
    {
        std::list<Creature*>::iterator it;
        for(it = creature_list.begin(); it != creature_list.end(); ++it)
        {
            if(*it == cc)
                break;
        }
        return (it == creature_list.end()) ? false : true;
    }

    list<Creature*> creature_list;
    Ogre::Vector2 index_point;
};

class Segment
{
public:
    Segment()
    {}

    Segment(Ogre::Vector2 tt, Ogre::Vector2 hh):
        tail(tt)
    {
        delta.x = hh.x - tt.x;
        delta.y = hh.y - tt.y;
    }

    Segment(Ogre::Vector3 tt, Ogre::Vector3 hh):
        tail(tt.x,tt.y)
    {
        delta.x = hh.x - tt.x;
        delta.y = hh.y - tt.y;
    }

    friend istream& operator>>( istream& is, Segment &ss)
    {
        is >> ss.tail.x;
        is >> ss.tail.y;
        return is;
    }

    void setDelta(Ogre::Vector2& dd)
    {
        delta.x = dd.x - tail.x;
        delta.y = dd.y - tail.y;
    }

    Ogre::Vector2 tail;
    Ogre::Vector2 delta;
};

class CullingQuad : public gms::FastPODAllocator<CullingQuad>
{
public:
    CullingQuad();
    CullingQuad(CullingQuad*,CullingQuad* = NULL);
    CullingQuad(CullingQuad&);
    ~CullingQuad();

    void nodes_sign_sort(int*&, int*&, int*&);
    bool reinsert(Entry* ee);

    inline void setChildsCenter()
    {
        for(int jj = 0; jj < 4; ++jj)
            nodes[jj]->setCenterFromParent(this, jj);
    }

    CullingQuad* find(Entry&);
    CullingQuad* find(Entry*);
    CullingQuad* find(Ogre::Vector2&);
    CullingQuad* find(Ogre::Vector2*);

    CullingQuad* insert(Entry*);
    CullingQuad* insert(Entry&);
    inline  CullingQuad* insert(Creature* cc)
    {
        return insert (new Entry(cc));
    }

    inline  CullingQuad* insert(Creature& cc)
    {
        return insert (new Entry(cc));
    }

    CullingQuad* shallowInsert(Entry*);
    CullingQuad* shallowInsert(Entry&);
    void setCenterFromParent(CullingQuad*, int);
    set<Creature*>* returnCreaturesSet(set<Creature*>*);
    void returnCreaturesSetAux(set<Creature*> *);

    bool cut(const Segment*);
    bool cut(const Segment&);
    void print();
    int countNodes();
    void setCenter(double, double);
    void setRadius(double);
    bool moveEntryDelta(Creature* ,const Ogre::Vector2&);
    void holdRootSemaphore();
    void releaseRootSemaphore();
    void mortuaryInsert(Creature*);

    Entry *entry;
    Ogre::Vector2 *center;
    CullingQuad **nodes;
    CullingQuad *parent;
    double mRadius;

protected:
    inline bool isEmptyLeaf() const
    {
        return isLeaf() && (entry == NULL || entry->creature_list.empty());
    }

    inline bool isNonEmptyLeaf() const
    {
        return isLeaf() && !(entry == NULL || entry->creature_list.empty());
    }

    inline bool isLeaf() const
    {
        return nodes == NULL;
    }

    CullingQuad* insertNoLock(Entry* ee);
};

// Debug output function
/*
istream& operator>>(istream &is, Ogre::Vector2& vv)
{
    is >> vv.x;
    is >> vv.y;
    return is;
}
*/

#endif // CULLINGQUAD_H
