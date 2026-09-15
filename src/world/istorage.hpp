#pragma once
#include <string>
#include "coord.hpp"
namespace Geo
{
class IStorage
{
public:
    IStorage();

    //add a map to the group of maps that constitue this "level"
    virtual bool add_to_level(std::string& map_name) = 0;
    // removes the map
    virtual bool remove_from_level(std::string& map_name) = 0;

    virtual Chunk&


};
}
