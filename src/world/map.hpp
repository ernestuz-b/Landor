#pragma once
#include "coord.hpp"
#include "area.hpp"
namespace Geo
{

/*
 * A map is loaded from storage in blocks.
 * Maps are patches of the world, they have a natural offset and sizes in metadata.
 * A map acquires its natural offsets when loaded if not overriden, but maps can
 * be made to overlap, in that case the last one wins.
 * Maps have layers, for instance terrain, height, etc, that are loaded from
 * different files if the storage is a filesystem, or whatever, because the storage is
 * abstracted.
 */

template<typename T> //T selecting the integer for the coords and sizes
class Map
{
public:
    Map();
    Area<T> area();
    Coord<T> position;
    void set_position(Coord<T>& position);
    void set_default_position();

};

}
