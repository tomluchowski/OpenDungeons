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

#ifndef ODCONSOLECOMMAND_H
#define ODCONSOLECOMMAND_H

#include "gamemap/GameMap.h"

class ODConsoleCommand
{
public:
    ODConsoleCommand() {}
    virtual ~ODConsoleCommand() {}
    virtual void execute(GameMap* gameMap) = 0;
};

class ODConsoleCommandAddGold : public ODConsoleCommand
{
public:
    ODConsoleCommandAddGold(int gold, int seatId) :
        mGold(gold),
        mSeatId(seatId)
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->addGoldToSeat(mGold, mSeatId);
    }

private:
    int mGold;
    int mSeatId;
};

class ODConsoleCommandLogFloodFill : public ODConsoleCommand
{
public:
    ODConsoleCommandLogFloodFill()
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->logFloodFileTiles();
    }
};

class ODConsoleCommandSetCreatureDestination : public ODConsoleCommand
{
public:
    ODConsoleCommandSetCreatureDestination(const std::string& creatureName, int x, int y):
        mCreatureName(creatureName),
        mX(x),
        mY(y)
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->consoleSetCreatureDestination(mCreatureName, mX, mY);
    }
private:
    std::string mCreatureName;
    int mX;
    int mY;
};

class ODConsoleCommandDisplayCreatureVisualDebug : public ODConsoleCommand
{
public:
    ODConsoleCommandDisplayCreatureVisualDebug(const std::string& creatureName, bool enable):
        mCreatureName(creatureName),
        mEnable(enable)
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->consoleDisplayCreatureVisualDebug(mCreatureName, mEnable);
    }
private:
    std::string mCreatureName;
    bool mEnable;
};

class ODConsoleCommandDisplaySeatVisualDebug : public ODConsoleCommand
{
public:
    ODConsoleCommandDisplaySeatVisualDebug(int seatId, bool enable):
        mSeatId(seatId),
        mEnable(enable)
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->consoleDisplaySeatVisualDebug(mSeatId, mEnable);
    }
private:
    int mSeatId;
    bool mEnable;
};

class ODConsoleCommandSetLevelCreature : public ODConsoleCommand
{
public:
    ODConsoleCommandSetLevelCreature(const std::string& creatureName, uint32_t level):
        mCreatureName(creatureName),
        mLevel(level)
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->consoleSetLevelCreature(mCreatureName, mLevel);
    }
private:
    std::string mCreatureName;
    uint32_t mLevel;
};

class ODConsoleCommandAskToggleFOW : public ODConsoleCommand
{
public:
    ODConsoleCommandAskToggleFOW()
    {
    }

protected:
    virtual void execute(GameMap* gameMap)
    {
        gameMap->consoleAskToggleFOW();
    }
};

#endif // ODCONSOLECOMMAND_H
