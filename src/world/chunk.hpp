#pragma once

namespace landor::world
{

// Placeholder only. DESIGN_STATE.md D-18…D-21 retire Chunk in favour of
// sector + pin; this skeleton exists so the header set compiles as a unit and
// is deleted when the storage-side interface lands.
class Chunk
{
public:
    Chunk();
};

} // namespace landor::world
