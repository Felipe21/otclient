/*
 * Copyright (c) 2010-2012 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "map.h"
#include "game.h"
#include "localplayer.h"
#include "tile.h"
#include "item.h"
#include "missile.h"
#include "statictext.h"

#include <framework/core/eventdispatcher.h>
#include "mapview.h"
#include <framework/core/resourcemanager.h>

Map g_map;

void Map::addMapView(const MapViewPtr& mapView)
{
    m_mapViews.push_back(mapView);
}

void Map::removeMapView(const MapViewPtr& mapView)
{
    auto it = std::find(m_mapViews.begin(), m_mapViews.end(), mapView);
    if(it != m_mapViews.end())
        m_mapViews.erase(it);
}

void Map::notificateTileUpdateToMapViews(const Position& pos)
{
    for(const MapViewPtr& mapView : m_mapViews)
        mapView->onTileUpdate(pos);
}

void Map::load()
{
    if(!g_resources.fileExists("/map.otcmap"))
        return;

    std::stringstream in;
    g_resources.loadFile("/map.otcmap", in);

    while(!in.eof()) {
        Position pos;
        in.read((char*)&pos, sizeof(pos));

        uint16 id;
        in.read((char*)&id, sizeof(id));
        while(id != 0xFFFF) {
            ItemPtr item = Item::create(id);
            if(item->isStackable() || item->isFluidContainer() || item->isFluid()) {
                uint8 data;
                in.read((char*)&data, sizeof(data));
                item->setCount(data);
            }
            addThing(item, pos, 255);
            in.read((char*)&id, sizeof(id));
        }
    }
}

void Map::save()
{
    std::stringstream out;

    for(auto& pair : m_tiles) {
        Position pos = pair.first;
        TilePtr tile = pair.second;
        if(!tile || tile->isEmpty())
            continue;
        out.write((char*)&pos, sizeof(pos));
        uint16 id;
        for(const ThingPtr& thing : tile->getThings()) {
            if(ItemPtr item = thing->asItem()) {
                id = item->getId();
                out.write((char*)&id, sizeof(id));
                if(item->isStackable() || item->isFluidContainer() || item->isFluid()) {
                    uint8 data = item->getCount();
                    out.write((char*)&data, sizeof(data));
                }
            }
        }
        id = 0xFFFF;
        out.write((char*)&id, sizeof(id));
    }

    g_resources.saveFile("/map.otcmap", out);
}

void Map::clean()
{
    m_tiles.clear();
    m_knownCreatures.clear();
    for(int i=0;i<=Otc::MAX_Z;++i)
        m_floorMissiles[i].clear();
    m_animatedTexts.clear();
    m_staticTexts.clear();
}

void Map::addThing(const ThingPtr& thing, const Position& pos, int stackPos)
{
    if(!thing)
        return;

    TilePtr tile = getTile(pos);
    if(!tile)
        tile = createTile(pos);

    if(CreaturePtr creature = thing->asCreature()) {
        Position oldPos = thing->getPosition();
        tile->addThing(thing, stackPos);

        // creature teleported
        if(oldPos.isValid() && !oldPos.isInRange(pos,1,1))
            g_game.processCreatureTeleport(creature);
    } else if(MissilePtr missile = thing->asMissile()) {
        m_floorMissiles[pos.z].push_back(missile);
    } else if(AnimatedTextPtr animatedText = thing->asAnimatedText()) {
        m_animatedTexts.push_back(animatedText);
    } else if(StaticTextPtr staticText = thing->asStaticText()) {
        bool mustAdd = true;
        for(auto it = m_staticTexts.begin(), end = m_staticTexts.end(); it != end; ++it) {
            StaticTextPtr cStaticText = *it;
            if(cStaticText->getPosition() == pos) {
                // try to combine messages
                if(cStaticText->addMessage(staticText->getName(), staticText->getMessageType(), staticText->getFirstMessage())) {
                    mustAdd = false;
                    break;
                } else {
                    // must add another message and rearrenge current
                }
            }

        }

        if(mustAdd)
            m_staticTexts.push_back(staticText);
    } else {
        tile->addThing(thing, stackPos);
    }

    thing->startAnimation();
    thing->setPosition(pos);

    notificateTileUpdateToMapViews(pos);
}

ThingPtr Map::getThing(const Position& pos, int stackPos)
{
    if(TilePtr tile = getTile(pos))
        return tile->getThing(stackPos);
    return nullptr;
}

bool Map::removeThing(const ThingPtr& thing)
{
    if(!thing) {
        return false;
    } else if(MissilePtr missile = thing->asMissile()) {
        auto it = std::find(m_floorMissiles[missile->getPosition().z].begin(), m_floorMissiles[missile->getPosition().z].end(), missile);
        if(it != m_floorMissiles[missile->getPosition().z].end()) {
            m_floorMissiles[missile->getPosition().z].erase(it);
            return true;
        }
    } else if(AnimatedTextPtr animatedText = thing->asAnimatedText()) {
        auto it = std::find(m_animatedTexts.begin(), m_animatedTexts.end(), animatedText);
        if(it != m_animatedTexts.end()) {
            m_animatedTexts.erase(it);
            return true;
        }
    } else if(StaticTextPtr staticText = thing->asStaticText()) {
        auto it = std::find(m_staticTexts.begin(), m_staticTexts.end(), staticText);
        if(it != m_staticTexts.end()) {
            m_staticTexts.erase(it);
            return true;
        }
    } else if(TilePtr tile = getTile(thing->getPosition()))
        return tile->removeThing(thing);

    notificateTileUpdateToMapViews(thing->getPosition());

    return false;
}

bool Map::removeThingByPos(const Position& pos, int stackPos)
{
    if(TilePtr tile = getTile(pos))
        return removeThing(tile->getThing(stackPos));
    return false;
}

TilePtr Map::createTile(const Position& pos)
{
    TilePtr tile = TilePtr(new Tile(pos));
    m_tiles[pos] = tile;
    return tile;
}

const TilePtr& Map::getTile(const Position& pos)
{
    auto it = m_tiles.find(pos);
    if(it != m_tiles.end())
        return it->second;
    static TilePtr nulltile;
    return nulltile;
}

void Map::cleanTile(const Position& pos)
{
    if(TilePtr tile = getTile(pos)) {
        tile->clean();
        m_tiles.erase(m_tiles.find(pos));

        notificateTileUpdateToMapViews(pos);
    }
}

void Map::addCreature(const CreaturePtr& creature)
{
    m_knownCreatures[creature->getId()] = creature;
}

CreaturePtr Map::getCreatureById(uint32 id)
{
    LocalPlayerPtr localPlayer = g_game.getLocalPlayer();
    if(localPlayer && localPlayer->getId() == id)
        return localPlayer;
    return m_knownCreatures[id];
}

void Map::removeCreatureById(uint32 id)
{
    if(id == 0)
        return;
    m_knownCreatures.erase(id);
}

void Map::setCentralPosition(const Position& centralPosition)
{
    bool teleported = !m_centralPosition.isInRange(centralPosition, 1,1);
    m_centralPosition = centralPosition;

    // remove all creatures when teleporting, the server will resend them again
    if(teleported) {
        for(const auto& pair : m_knownCreatures) {
            const CreaturePtr& creature = pair.second;
            const TilePtr& tile = creature->getCurrentTile();
            if(tile) {
                tile->removeThing(creature);
                creature->setPosition(Position());
            }
        }
    // remove creatures from tiles that we are not aware anymore
    } else {
        for(const auto& pair : m_knownCreatures) {
            const CreaturePtr& creature = pair.second;
            if(!isAwareOfPosition(creature->getPosition())) {
                const TilePtr& tile = creature->getCurrentTile();
                if(tile) {
                    tile->removeThing(creature);
                    creature->setPosition(Position());
                }
            }
        }
    }
}

std::vector<CreaturePtr> Map::getSpectators(const Position& centerPos, bool multiFloor)
{
    return getSpectatorsInRange(centerPos, multiFloor, (Otc::VISIBLE_X_TILES - 1)/2, (Otc::VISIBLE_Y_TILES - 1)/2);
}

std::vector<CreaturePtr> Map::getSpectatorsInRange(const Position& centerPos, bool multiFloor, int xRange, int yRange)
{
    return getSpectatorsInRangeEx(centerPos, multiFloor, xRange, xRange, yRange, yRange);
}

std::vector<CreaturePtr> Map::getSpectatorsInRangeEx(const Position& centerPos, bool multiFloor, int minXRange, int maxXRange, int minYRange, int maxYRange)
{
    int minZRange = 0;
    int maxZRange = 0;
    std::vector<CreaturePtr> creatures;

    if(multiFloor) {
        minZRange = 0;
        maxZRange = Otc::MAX_Z;
    }

    //TODO: get creatures from other floors corretly

    for(int iz=-minZRange; iz<=maxZRange; ++iz) {
        for(int iy=-minYRange; iy<=maxYRange; ++iy) {
            for(int ix=-minXRange; ix<=maxXRange; ++ix) {
                TilePtr tile = getTile(centerPos.translated(ix,iy,iz));
                if(!tile)
                    continue;

                auto tileCreatures = tile->getCreatures();
                creatures.insert(creatures.end(), tileCreatures.rbegin(), tileCreatures.rend());
            }
        }
    }

    return creatures;
}

bool Map::isLookPossible(const Position& pos)
{
    TilePtr tile = getTile(pos);
    return tile && tile->isLookPossible();
}

bool Map::isCovered(const Position& pos, int firstFloor)
{
    // check for tiles on top of the postion
    Position tilePos = pos;
    while(tilePos.coveredUp() && tilePos.z >= firstFloor) {
        TilePtr tile = getTile(tilePos);
        // the below tile is covered when the above tile has a full ground
        if(tile && tile->isFullGround())
            return true;
    }
    return false;
}

bool Map::isCompletelyCovered(const Position& pos, int firstFloor)
{
    Position tilePos = pos;
    while(tilePos.coveredUp() && tilePos.z >= firstFloor) {
        bool covered = true;
        // check in 2x2 range tiles that has no transparent pixels
        for(int x=0;x<2;++x) {
            for(int y=0;y<2;++y) {
                const TilePtr& tile = getTile(tilePos.translated(-x, -y));
                if(!tile || !tile->isFullyOpaque()) {
                    covered = false;
                    break;
                }
            }
        }
        if(covered)
            return true;
    }
    return false;
}

bool Map::isAwareOfPosition(const Position& pos)
{
    if(pos.z < getFirstAwareFloor() || pos.z > getLastAwareFloor())
        return false;

    Position groundedPos = pos;
    while(groundedPos.z != m_centralPosition.z) {
        if(groundedPos.z > m_centralPosition.z)
            groundedPos.coveredUp();
        else
            groundedPos.coveredDown();
    }
    return m_centralPosition.isInRange(groundedPos, Otc::AWARE_X_LEFT_TILES,
                                                    Otc::AWARE_X_RIGHT_TILES,
                                                    Otc::AWARE_Y_TOP_TILES,
                                                    Otc::AWARE_Y_BOTTOM_TILES);
}

int Map::getFirstAwareFloor()
{
    if(m_centralPosition.z > Otc::SEA_FLOOR)
        return m_centralPosition.z-Otc::AWARE_UNDEGROUND_FLOOR_RANGE;
    else
        return 0;
}

int Map::getLastAwareFloor()
{
    if(m_centralPosition.z > Otc::SEA_FLOOR)
        return std::min(m_centralPosition.z+Otc::AWARE_UNDEGROUND_FLOOR_RANGE, (int)Otc::MAX_Z);
    else
        return Otc::SEA_FLOOR;
}
