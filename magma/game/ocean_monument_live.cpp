#include "game/ocean_monument_live.h"

/* Cold, allocation-bounded port of the Minecraft 1.11.2
 * StructureOceanMonumentPieces algorithm. Local real-Java fixtures lock the
 * complete clipped block volume, room graph side effects, and elder sites. */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mc_rng.h"

namespace {

enum {
    F_DOWN = 0, F_UP = 1, F_NORTH = 2,
    F_SOUTH = 3, F_WEST = 4, F_EAST = 5
};

struct Facing {
    enum { NORTH = F_NORTH, SOUTH = F_SOUTH, WEST = F_WEST, EAST = F_EAST };
};

struct Direction {
    static const int FACING_DIRECTION[6];
};
const int Direction::FACING_DIRECTION[6] = {0, 1, 2, 3, 4, 5};

struct Tile {
    enum {
        water_Id = 9,
        goldBlock_Id = 41,
        sponge_Id = 19,
        prismarine_Id = 168,
        seaLantern_Id = 169
    };
};

class Random {
    JavaRandom value_;
public:
    explicit Random(long long seed = 0) { setSeed(seed); }
    void setSeed(long long seed) { jrand_set(&value_, (i64)seed); }
    int nextInt() { return (int)jrand_int(&value_); }
    int nextInt(int bound) { return (int)jrand_int_bound(&value_, bound); }
    long long nextLong() { return (long long)jrand_long(&value_); }
    bool nextBoolean() { return jrand_next(&value_, 1) != 0; }
};

class BoundingBox {
public:
    int x0, y0, z0, x1, y1, z1;
    BoundingBox(int ax, int ay, int az, int bx, int by, int bz)
        : x0(std::min(ax, bx)), y0(std::min(ay, by)), z0(std::min(az, bz)),
          x1(std::max(ax, bx)), y1(std::max(ay, by)), z1(std::max(az, bz)) {}
    void move(int x, int y, int z) {
        x0 += x; x1 += x; y0 += y; y1 += y; z0 += z; z1 += z;
    }
    bool isInside(int x, int y, int z) const {
        return x >= x0 && x <= x1 && y >= y0 && y <= y1
            && z >= z0 && z <= z1;
    }
    bool intersects(const BoundingBox *other) const {
        return x1 >= other->x0 && x0 <= other->x1
            && y1 >= other->y0 && y0 <= other->y1
            && z1 >= other->z0 && z0 <= other->z1;
    }
    bool intersects(int ax, int az, int bx, int bz) const {
        return x1 >= ax && x0 <= bx && z1 >= az && z0 <= bz;
    }
};

class Level {
    const GmMonumentAccess *access_;
public:
    explicit Level(const GmMonumentAccess *access) : access_(access) {}
    int getTile(int x, int y, int z) const {
        return access_->get_block(access_->opaque, x, y, z) >> 4;
    }
    int getSeaLevel() const { return access_->sea_level; }
    void setTile(int x, int y, int z, int id, int meta) {
        access_->set_block(access_->opaque, x, y, z,
            (unsigned short)((id << 4) | (meta & 15)));
    }
    void spawnElder(double x, double y, double z) {
        if (access_->spawn_elder)
            access_->spawn_elder(access_->opaque, x, y, z);
    }
};

class StructurePiece {
protected:
    int orientation;
    BoundingBox *boundingBox;
public:
    explicit StructurePiece(int) : orientation(F_NORTH), boundingBox(nullptr) {}
    virtual ~StructurePiece() { delete boundingBox; }
    BoundingBox *getBoundingBox() const { return boundingBox; }
    virtual bool postProcess(Level *, Random *, BoundingBox *) = 0;

protected:
    int getWorldX(int x, int z) const {
        if (orientation == F_NORTH || orientation == F_SOUTH)
            return boundingBox->x0 + x;
        if (orientation == F_WEST) return boundingBox->x1 - z;
        if (orientation == F_EAST) return boundingBox->x0 + z;
        return x;
    }
    int getWorldY(int y) const { return boundingBox->y0 + y; }
    int getWorldZ(int x, int z) const {
        if (orientation == F_NORTH) return boundingBox->z1 - z;
        if (orientation == F_SOUTH) return boundingBox->z0 + z;
        if (orientation == F_WEST || orientation == F_EAST)
            return boundingBox->z0 + x;
        return z;
    }
    void placeBlock(Level *level, int id, int meta,
                    int x, int y, int z, BoundingBox *clip) {
        int wx = getWorldX(x, z), wy = getWorldY(y), wz = getWorldZ(x, z);
        if (clip->isInside(wx, wy, wz))
            level->setTile(wx, wy, wz, id, meta);
    }
    void generateBox(Level *level, BoundingBox *clip,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     int id, int meta, bool existingOnly) {
        generateBox(level, clip, x0, y0, z0, x1, y1, z1,
                    id, meta, id, meta, existingOnly);
    }
    void generateBox(Level *level, BoundingBox *clip,
                     int x0, int y0, int z0, int x1, int y1, int z1,
                     int boundaryId, int boundaryMeta,
                     int insideId, int insideMeta, bool existingOnly) {
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                for (int z = z0; z <= z1; ++z) {
                    int wx = getWorldX(x, z), wy = getWorldY(y);
                    int wz = getWorldZ(x, z);
                    if (!clip->isInside(wx, wy, wz)
                            || (existingOnly && level->getTile(wx, wy, wz) == 0))
                        continue;
                    bool inside = y != y0 && y != y1 && x != x0
                        && x != x1 && z != z0 && z != z1;
                    level->setTile(wx, wy, wz,
                        inside ? insideId : boundaryId,
                        inside ? insideMeta : boundaryMeta);
                }
    }
    void fillColumnDown(Level *level, int id, int meta,
                        int x, int y, int z, BoundingBox *clip) {
        int wx = getWorldX(x, z), wy = getWorldY(y), wz = getWorldZ(x, z);
        if (!clip->isInside(wx, wy, wz)) return;
        while (wy > 1) {
            int block = level->getTile(wx, wy, wz);
            if (block != 0 && block != 8 && block != 9
                    && block != 10 && block != 11)
                break;
            level->setTile(wx, wy, wz, id, meta);
            --wy;
        }
    }
};

template <class T>
static void javaShuffle(std::vector<T> &list, Random *random) {
    for (int i = (int)list.size(); i > 1; --i) {
        int j = random->nextInt(i);
        std::swap(list[(size_t)i - 1], list[(size_t)j]);
    }
}


class OceanMonumentPieces
{
public:


    // field_175967_a = index
    // field_175965_b = neighbors[6]
    // field_175966_c = hasOpening[6]
    // field_175963_d = claimed
    // field_175964_e = isEntrance
    // field_175962_f = visitedTag

    struct RoomDefinition
    {
        int   index;           // field_175967_a
        RoomDefinition* neighbors[6];  // field_175965_b
        bool  hasOpening[6];           // field_175966_c
        bool  claimed;                 // field_175963_d
        bool  isEntrance;              // field_175964_e
        int   visitedTag;              // field_175962_f

        RoomDefinition(int idx) : index(idx), claimed(false), isEntrance(false), visitedTag(0)
        {
            memset(hasOpening, 0, sizeof(hasOpening));
            memset(neighbors,  0, sizeof(neighbors));
        }

        // func_175957_a
        void connectTo(int facing, RoomDefinition* other)
        {
             this->neighbors[facing] = other;

        int opposite = (facing % 2 == 0) ? facing + 1 : facing - 1;
        other->neighbors[opposite] = this;
        }

        // func_175958_a
        void initOpenings()
        {
            for (int i = 0; i < 6; ++i)
                hasOpening[i] = (neighbors[i] != nullptr);
        }

        // func_175959_a
        bool isReachable(int tag)
        {
            if (isEntrance) return true;
            visitedTag = tag;
            for (int i = 0; i < 6; ++i)
            {
                if (neighbors[i] != nullptr && hasOpening[i] &&
                    neighbors[i]->visitedTag != tag &&
                    neighbors[i]->isReachable(tag))
                    return true;
            }
            return false;
        }

        // func_175961_b
        bool isSpecial() const { return index >= 75; }

        // func_175960_c
        int countOpenings() const
        {
            int c = 0;
            for (int i = 0; i < 6; ++i)
                if (hasOpening[i]) ++c;
            return c;
        }

    private:
        static int oppositeFacing(int f)
        {
            // DOWN=0 UP=1 NORTH=2 SOUTH=3 WEST=4 EAST=5
            static const int opp[6] = { 1, 0, 3, 2, 5, 4 };
            return opp[f];
        }
    };


    static int blockPrismarine();
    static int blockPrismarineBricks();
    static int blockDarkPrismarine();
    static int blockWater();
    static int blockSeaLantern();
    static int blockGoldBlock();
    static int blockSponge();
    static int blockAir();


    class Piece : public StructurePiece
    {
    protected:
        RoomDefinition* roomDef; // field_175830_k

        // func_175820_a(x, y, z) = y*25 + z*5 + x
        static int roomIndex(int x, int y, int z) { return y * 25 + z * 5 + x; }

        static const int ENTRY_INDEX;   // field_175823_g = roomIndex(2,0,0)
        static const int CORE_INDEX;    // field_175831_h = roomIndex(2,2,0)
        static const int WING1_INDEX;   // field_175832_i = roomIndex(0,1,0)
        static const int WING2_INDEX;   // field_175829_j = roomIndex(4,1,0)

        Piece();
        Piece(int type);
        Piece(int facing, BoundingBox* box);
        Piece(int type, int facing, RoomDefinition* room, int sizeX, int sizeY, int sizeZ);

        // func_181655_a
        void fillWithAirOrWater(Level* level, BoundingBox* bb, int x0, int y0, int z0, int x1, int y1, int z1, bool onlySolid);

        // func_175821_a
        void generateFloor(Level* level, BoundingBox* bb, int offX, int offZ, bool hasOpening);

        // func_175819_a
        void fillWaterWith(Level* level, BoundingBox* bb, int x0, int y0, int z0, int x1, int y1, int z1, int block);

        // func_175818_a
        bool intersectsXZ(BoundingBox* chunkBB, int x0, int z0, int x1, int z1);

        // func_175817_a
        bool spawnElderGuardian(Level* level, BoundingBox* bb, int x, int y, int z);

    public:
    };


    struct RoomFitHelper
    {
        virtual bool canFit(RoomDefinition* room) = 0;
        virtual Piece* createPiece(int facing, RoomDefinition* room, Random* random) = 0;
        virtual ~RoomFitHelper() {}
    };


    class MonumentBuilding : public Piece
    {
    private:
        static const int ARRAY_SIZE = 75; // 5*5*3
        RoomDefinition* entryRoom;  // field_175845_o
        RoomDefinition* coreRoom;   // field_175844_p
        std::vector<Piece*> subPieces; // field_175843_q
        std::vector<RoomDefinition*> roomDefs;

        std::vector<RoomDefinition*> buildRoomGrid(Random* random);


        void buildWing(bool isRight, int offsetX, Level* level, Random* random, BoundingBox* chunkBB);
        void buildEntrance(Level* level, Random* random, BoundingBox* chunkBB);
        void buildTopSection(Level* level, Random* random, BoundingBox* chunkBB);
        void buildCorridorCenter(Level* level, Random* random, BoundingBox* chunkBB);
        void buildSideCorridor(Level* level, Random* random, BoundingBox* chunkBB);
        void buildInnerCorridor(Level* level, Random* random, BoundingBox* chunkBB);
        void buildUpperSection(Level* level, Random* random, BoundingBox* chunkBB);

    public:

        MonumentBuilding();
        MonumentBuilding(Random* random, int x, int z, int facing);
        ~MonumentBuilding();

        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };

    class CoreRoom : public Piece {
    public:
        CoreRoom();
        CoreRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class SimpleRoom : public Piece {
    private:
        int variant; // field_175833_o
    public:
        SimpleRoom();
        SimpleRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };

    class SimpleTopRoom : public Piece {
    public:
        SimpleTopRoom();
        SimpleTopRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class DoubleXRoom : public Piece {
    public:
        DoubleXRoom();
        DoubleXRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class DoubleXYRoom : public Piece {
    public:
        DoubleXYRoom();
        DoubleXYRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class DoubleYRoom : public Piece {
    public:
        DoubleYRoom();
        DoubleYRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class DoubleYZRoom : public Piece {
    public:
        DoubleYZRoom();
        DoubleYZRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class DoubleZRoom : public Piece {
    public:
        DoubleZRoom();
        DoubleZRoom(int facing, RoomDefinition* room, Random* random);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };



    class EntryRoom : public Piece {
    public:
        EntryRoom();
        EntryRoom(int facing, RoomDefinition* room);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class Penthouse : public Piece {
    public:
        Penthouse();
        Penthouse(int facing, BoundingBox* box);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };


    class WingRoom : public Piece {
    private:
        int wingType; // field_175834_o = p_i45585_3_ & 1
    public:
        WingRoom();
        WingRoom(int facing, BoundingBox* box, int seed);
        bool postProcess(Level* level, Random* random, BoundingBox* chunkBB) override;
    };
};

int OceanMonumentPieces::blockPrismarine()       { return 0; }
int OceanMonumentPieces::blockPrismarineBricks() { return 1; }
int OceanMonumentPieces::blockDarkPrismarine()   { return 2; }
int OceanMonumentPieces::blockWater()            { return Tile::water_Id; }
int OceanMonumentPieces::blockSeaLantern()       { return Tile::seaLantern_Id; }
int OceanMonumentPieces::blockGoldBlock()        { return Tile::goldBlock_Id; }
int OceanMonumentPieces::blockSponge()           { return Tile::sponge_Id; }
int OceanMonumentPieces::blockAir()              { return 0; }

bool OceanMonumentPieces::Piece::spawnElderGuardian(
        Level *level, BoundingBox *clip, int x, int y, int z) {
    int wx = getWorldX(x, z), wy = getWorldY(y), wz = getWorldZ(x, z);
    if (!clip->isInside(wx, wy, wz)) return false;
    level->spawnElder(wx + 0.5, wy, wz + 0.5);
    return true;
}


// DOWN=0, UP=1, NORTH=2, SOUTH=3, WEST=4, EAST=5

#define F_DOWN  0
#define F_UP    1
#define F_NORTH 2
#define F_SOUTH 3
#define F_WEST  4
#define F_EAST  5


// func_175820_a(x, y, z) = y*25 + z*5 + x

const int OceanMonumentPieces::Piece::ENTRY_INDEX = 2;          // roomIndex(2,0,0)
const int OceanMonumentPieces::Piece::CORE_INDEX  = 52;         // roomIndex(2,2,0)
const int OceanMonumentPieces::Piece::WING1_INDEX = 25;         // roomIndex(0,1,0)
const int OceanMonumentPieces::Piece::WING2_INDEX = 29;         // roomIndex(4,1,0)




OceanMonumentPieces::Piece::Piece() : StructurePiece(0), roomDef(nullptr) {}

OceanMonumentPieces::Piece::Piece(int type) : StructurePiece(type), roomDef(nullptr) {}


OceanMonumentPieces::Piece::Piece(int facing, BoundingBox* box)
    : StructurePiece(1), roomDef(nullptr)
{

    orientation = Direction::FACING_DIRECTION[facing];
    boundingBox = box;
}


OceanMonumentPieces::Piece::Piece(int type, int facing, RoomDefinition* room, int sizeX, int sizeY, int sizeZ)
    : StructurePiece(type), roomDef(room)
{

    orientation = Direction::FACING_DIRECTION[facing];

    if (room == nullptr) return;

    int i = room->index;
    int j = i % 5;
    int k = (i / 5) % 5;
    int l = i / 25;


    if (facing != Facing::NORTH && facing != Facing::SOUTH)
        boundingBox = new BoundingBox(0, 0, 0, sizeZ * 8 - 1, sizeY * 4 - 1, sizeX * 8 - 1);
    else
        boundingBox = new BoundingBox(0, 0, 0, sizeX * 8 - 1, sizeY * 4 - 1, sizeZ * 8 - 1);

    switch (facing)
    {
    case Facing::NORTH:
        boundingBox->move(j * 8, l * 4, -(k + sizeZ) * 8 + 1);
        break;
    case Facing::SOUTH:
        boundingBox->move(j * 8, l * 4, k * 8);
        break;
    case Facing::WEST:
        boundingBox->move(-(k + sizeZ) * 8 + 1, l * 4, j * 8);
        break;
    default: // EAST
        boundingBox->move(k * 8, l * 4, j * 8);
        break;
    }
}



void OceanMonumentPieces::Piece::fillWithAirOrWater(Level* level, BoundingBox* bb,
    int x0, int y0, int z0, int x1, int y1, int z1, bool onlySolid)
{
    for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
    for (int z = z0; z <= z1; ++z)
    {
        if (!onlySolid || level->getTile(getWorldX(x, z), getWorldY(y), getWorldZ(x, z)) != 0)
        {
            if (getWorldY(y) >= level->getSeaLevel())
                placeBlock(level, blockAir(), 0, x, y, z, bb);
            else
                placeBlock(level, blockWater(), 0, x, y, z, bb);
        }
    }
}


void OceanMonumentPieces::Piece::generateFloor(Level* level, BoundingBox* bb, int offX, int offZ, bool hasOpening)
{
    if (hasOpening)
    {
        generateBox(level, bb, offX + 0, 0, offZ + 0, offX + 2, 0, offZ + 7, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, bb, offX + 5, 0, offZ + 0, offX + 7, 0, offZ + 7, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, bb, offX + 3, 0, offZ + 0, offX + 4, 0, offZ + 1, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, bb, offX + 3, 0, offZ + 5, offX + 4, 0, offZ + 7, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, bb, offX + 3, 0, offZ + 2, offX + 4, 0, offZ + 2, Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, bb, offX + 3, 0, offZ + 5, offX + 4, 0, offZ + 5, Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, bb, offX + 2, 0, offZ + 3, offX + 2, 0, offZ + 4, Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, bb, offX + 5, 0, offZ + 3, offX + 5, 0, offZ + 4, Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
    else
    {
        generateBox(level, bb, offX + 0, 0, offZ + 0, offX + 7, 0, offZ + 7, Tile::prismarine_Id, blockPrismarine(), false);
    }
}


void OceanMonumentPieces::Piece::fillWaterWith(Level* level, BoundingBox* bb,
    int x0, int y0, int z0, int x1, int y1, int z1, int block)
{
    for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x)
    for (int z = z0; z <= z1; ++z)
    {
        if (level->getTile(getWorldX(x, z), getWorldY(y), getWorldZ(x, z)) == blockWater())
            placeBlock(level, block, 0, x, y, z, bb);
    }
}


bool OceanMonumentPieces::Piece::intersectsXZ(BoundingBox* chunkBB, int x0, int z0, int x1, int z1)
{
    int wx0 = getWorldX(x0, z0);
    int wz0 = getWorldZ(x0, z0);
    int wx1 = getWorldX(x1, z1);
    int wz1 = getWorldZ(x1, z1);
    return chunkBB->intersects(
        (std::min)(wx0, wx1), (std::min)(wz0, wz1),
        (std::max)(wx0, wx1), (std::max)(wz0, wz1));
}




OceanMonumentPieces::MonumentBuilding::MonumentBuilding()
    : entryRoom(nullptr), coreRoom(nullptr) {}

OceanMonumentPieces::MonumentBuilding::MonumentBuilding(Random* random, int x, int z, int facing)
    : Piece(0), entryRoom(nullptr), coreRoom(nullptr)
{

    this->orientation = Direction::FACING_DIRECTION[facing];
    this->boundingBox = new BoundingBox(x, 39, z, x + 57, 61, z + 57);


    std::vector<RoomDefinition*> roomList = buildRoomGrid(random);
    roomDefs = roomList;


    entryRoom->claimed = true;
    subPieces.push_back(new EntryRoom(facing, entryRoom));
    subPieces.push_back(new CoreRoom(facing, coreRoom, random));


    struct XYFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return r->hasOpening[F_EAST] && !r->neighbors[F_EAST]->claimed &&
                   r->hasOpening[F_UP]   && !r->neighbors[F_UP]->claimed &&
                   r->neighbors[F_EAST]->hasOpening[F_UP] &&
                   !r->neighbors[F_EAST]->neighbors[F_UP]->claimed;
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            r->neighbors[F_EAST]->claimed = true;
            r->neighbors[F_UP]->claimed = true;
            r->neighbors[F_EAST]->neighbors[F_UP]->claimed = true;
            return new DoubleXYRoom(facing, r, rng);
        }
    };
    struct YZFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return r->hasOpening[F_NORTH] && !r->neighbors[F_NORTH]->claimed &&
                   r->hasOpening[F_UP]    && !r->neighbors[F_UP]->claimed &&
                   r->neighbors[F_NORTH]->hasOpening[F_UP] &&
                   !r->neighbors[F_NORTH]->neighbors[F_UP]->claimed;
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            r->neighbors[F_NORTH]->claimed = true;
            r->neighbors[F_UP]->claimed = true;
            r->neighbors[F_NORTH]->neighbors[F_UP]->claimed = true;
            return new DoubleYZRoom(facing, r, rng);
        }
    };
    struct ZFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return r->hasOpening[F_NORTH] && !r->neighbors[F_NORTH]->claimed;
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            RoomDefinition* base = r;
            if (!r->hasOpening[F_NORTH] || r->neighbors[F_NORTH]->claimed)
                base = r->neighbors[F_SOUTH];
            base->claimed = true;
            base->neighbors[F_NORTH]->claimed = true;
            return new DoubleZRoom(facing, base, rng);
        }
    };
    struct XFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return r->hasOpening[F_EAST] && !r->neighbors[F_EAST]->claimed;
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            r->neighbors[F_EAST]->claimed = true;
            return new DoubleXRoom(facing, r, rng);
        }
    };
    struct YFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return r->hasOpening[F_UP] && !r->neighbors[F_UP]->claimed;
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            r->neighbors[F_UP]->claimed = true;
            return new DoubleYRoom(facing, r, rng);
        }
    };
    struct SimpleTopFit : RoomFitHelper {
        bool canFit(RoomDefinition* r) override {
            return !r->hasOpening[F_WEST]  && !r->hasOpening[F_EAST] &&
                   !r->hasOpening[F_NORTH] && !r->hasOpening[F_SOUTH] &&
                   !r->hasOpening[F_UP];
        }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            return new SimpleTopRoom(facing, r, rng);
        }
    };
    struct SimpleFit : RoomFitHelper {
        bool canFit(RoomDefinition*) override { return true; }
        Piece* createPiece(int facing, RoomDefinition* r, Random* rng) override {
            r->claimed = true;
            return new SimpleRoom(facing, r, rng);
        }
    };

    std::vector<RoomFitHelper*> helpers = {
        new XYFit(), new YZFit(), new ZFit(), new XFit(), new YFit(),
        new SimpleTopFit(), new SimpleFit()
    };


    for (RoomDefinition* room : roomList)
    {
        if (!room->claimed && !room->isSpecial())
        {
            for (RoomFitHelper* h : helpers)
            {
                if (h->canFit(room))
                {
                    subPieces.push_back(h->createPiece(facing, room, random));
                    break;
                }
            }
        }
    }

    for (RoomFitHelper* h : helpers) delete h;


    int minY  = boundingBox->y0;
    int baseX = getWorldX(9, 22);
    int baseZ = getWorldZ(9, 22);

    for (Piece* p : subPieces)
        p->getBoundingBox()->move(baseX, minY, baseZ);


    BoundingBox* wing1BB = new BoundingBox(
        getWorldX(1,  1),  getWorldY(1),  getWorldZ(1,  1),
        getWorldX(23, 21), getWorldY(8),  getWorldZ(23, 21));
    BoundingBox* wing2BB = new BoundingBox(
        getWorldX(34, 1),  getWorldY(1),  getWorldZ(34, 1),
        getWorldX(56, 21), getWorldY(8),  getWorldZ(56, 21));
    BoundingBox* penthouseBB = new BoundingBox(
        getWorldX(22, 22), getWorldY(13), getWorldZ(22, 22),
        getWorldX(35, 35), getWorldY(17), getWorldZ(35, 35));

    int seed = random->nextInt();
    subPieces.push_back(new WingRoom(facing, wing1BB, seed++));
    subPieces.push_back(new WingRoom(facing, wing2BB, seed++));
    subPieces.push_back(new Penthouse(facing, penthouseBB));
    delete wing1BB;
    delete wing2BB;
    delete penthouseBB;
}

OceanMonumentPieces::MonumentBuilding::~MonumentBuilding()
{
    for (Piece* p : subPieces) delete p;
    for (RoomDefinition* room : roomDefs) delete room;
}


std::vector<OceanMonumentPieces::RoomDefinition*> OceanMonumentPieces::MonumentBuilding::buildRoomGrid(Random* random)
{
    RoomDefinition* grid[75] = {};


    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 4; ++z)
        {
            int idx = Piece::roomIndex(x, 0, z);
            grid[idx] = new RoomDefinition(idx);
        }


    for (int x = 0; x < 5; ++x)
        for (int z = 0; z < 4; ++z)
        {
            int idx = Piece::roomIndex(x, 1, z);
            grid[idx] = new RoomDefinition(idx);
        }


    for (int x = 1; x < 4; ++x)
        for (int z = 0; z < 2; ++z)
        {
            int idx = Piece::roomIndex(x, 2, z);
            grid[idx] = new RoomDefinition(idx);
        }


    entryRoom = grid[ENTRY_INDEX];


    for (int x = 0; x < 5; ++x)
    for (int y = 0; y < 3; ++y)
    for (int z = 0; z < 5; ++z)
    {
        int idx = Piece::roomIndex(x, y, z);
        if (grid[idx] == nullptr) continue;


        static const int dx[6] = { 0, 0, 0, 0,-1, 1 };
        static const int dy[6] = {-1, 1, 0, 0, 0, 0 };
        static const int dz[6] = { 0, 0,-1, 1, 0, 0 };
        static const int oppFacing[6] = { 1, 0, 3, 2, 5, 4 };

        for (int f = 0; f < 6; ++f)
        {
            int nx = x + dx[f];
            int ny = y + dy[f];
            int nz = z + dz[f];
            if (nx < 0 || nx >= 5 || ny < 0 || ny >= 3 || nz < 0 || nz >= 5) continue;
            int nidx = Piece::roomIndex(nx, ny, nz);
            if (grid[nidx] == nullptr) continue;


            if (nidx <= idx) continue;

            if (nz != z)
            {

                int opp = (f % 2 == 0) ? f + 1 : f - 1;
                grid[idx]->connectTo(opp, grid[nidx]);
            }
            else
            {
                grid[idx]->connectTo(f, grid[nidx]);
            }
        }
    }


    RoomDefinition* specialUp    = new RoomDefinition(1003);
    RoomDefinition* specialWing1 = new RoomDefinition(1001);
    RoomDefinition* specialWing2 = new RoomDefinition(1002);
    grid[CORE_INDEX ]->connectTo(F_UP,    specialUp);
    grid[WING1_INDEX]->connectTo(F_SOUTH, specialWing1);
    grid[WING2_INDEX]->connectTo(F_SOUTH, specialWing2);
    specialUp->claimed    = true;
    specialWing1->claimed = true;
    specialWing2->claimed = true;

    entryRoom->isEntrance = true;


    coreRoom = grid[Piece::roomIndex(random->nextInt(4), 0, 2)];
    coreRoom->claimed = true;
    coreRoom->neighbors[F_EAST]->claimed  = true;
    coreRoom->neighbors[F_NORTH]->claimed = true;
    coreRoom->neighbors[F_EAST]->neighbors[F_NORTH]->claimed = true;
    coreRoom->neighbors[F_UP]->claimed = true;
    coreRoom->neighbors[F_EAST]->neighbors[F_UP]->claimed = true;
    coreRoom->neighbors[F_NORTH]->neighbors[F_UP]->claimed = true;
    coreRoom->neighbors[F_EAST]->neighbors[F_NORTH]->neighbors[F_UP]->claimed = true;


    std::vector<RoomDefinition*> list;
    for (int i = 0; i < 75; ++i)
    {
        if (grid[i] != nullptr)
        {
            grid[i]->initOpenings();
            list.push_back(grid[i]);
        }
    }
    specialUp->initOpenings();


    javaShuffle(list, random);


    int tagCounter = 1;
    for (RoomDefinition* room : list)
    {
        int removedCount = 0;
        int attempts = 0;
        while (removedCount < 2 && attempts < 5)
        {
            ++attempts;
            int f = random->nextInt(6);
            if (!room->hasOpening[f]) continue;

            int opp = (f % 2 == 0) ? f + 1 : f - 1;
            room->hasOpening[f] = false;
            room->neighbors[f]->hasOpening[opp] = false;

            if (room->isReachable(tagCounter++) && room->neighbors[f]->isReachable(tagCounter++))
                ++removedCount;
            else
            {

                room->hasOpening[f] = true;
                room->neighbors[f]->hasOpening[opp] = true;
            }
        }
    }

    list.push_back(specialUp);
    list.push_back(specialWing1);
    list.push_back(specialWing2);
    return list;
}

bool OceanMonumentPieces::MonumentBuilding::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    int seaLevel = (std::max)(level->getSeaLevel(), 64);
    int fillHeight = seaLevel - boundingBox->y0;


    fillWithAirOrWater(level, chunkBB, 0, 0, 0, 58, fillHeight, 58, false);


    buildWing(false, 0,  level, random, chunkBB);
    buildWing(true,  33, level, random, chunkBB);

    buildEntrance(level, random, chunkBB);
    buildTopSection(level, random, chunkBB);
    buildCorridorCenter(level, random, chunkBB);
    buildSideCorridor(level, random, chunkBB);
    buildInnerCorridor(level, random, chunkBB);
    buildUpperSection(level, random, chunkBB);


    for (int j = 0; j < 7; ++j)
    {
        int k = 0;
        while (k < 7)
        {
            if (k == 0 && j == 3) k = 6;

            int lx = j * 9;
            int lz = k * 9;
            for (int jj = 0; jj < 4; ++jj)
            for (int kk = 0; kk < 4; ++kk)
            {
                placeBlock(level, Tile::prismarine_Id,blockPrismarineBricks(), lx+jj, 0, lz+kk, chunkBB);
                fillColumnDown(level,Tile::prismarine_Id, blockPrismarineBricks(),  lx+jj, -1, lz+kk, chunkBB);
            }

            if (j != 0 && j != 6) k += 6;
            else ++k;
        }
    }


    for (int s = 0; s < 5; ++s)
    {
        fillWithAirOrWater(level, chunkBB, -1-s, 0+s*2, -1-s, -1-s, 23, 58+s, false);
        fillWithAirOrWater(level, chunkBB, 58+s, 0+s*2, -1-s, 58+s, 23, 58+s, false);
        fillWithAirOrWater(level, chunkBB,  0-s, 0+s*2, -1-s, 57+s, 23, -1-s, false);
        fillWithAirOrWater(level, chunkBB,  0-s, 0+s*2, 58+s, 57+s, 23, 58+s, false);
    }


    for (Piece* p : subPieces)
    {
        if (p->getBoundingBox()->intersects(chunkBB))
            p->postProcess(level, random, chunkBB);
    }

    return true;
}

// func_175840_a
void OceanMonumentPieces::MonumentBuilding::buildWing(bool isRight, int offX, Level* level, Random* random, BoundingBox* chunkBB)
{
    if (!intersectsXZ(chunkBB, offX, 0, offX + 23, 20)) return;

    generateBox(level, chunkBB, offX+0, 0, 0, offX+24, 0, 20, Tile::prismarine_Id, blockPrismarine(), false);
    fillWithAirOrWater(level, chunkBB, offX+0, 1, 0, offX+24, 10, 20, false);

    for (int j = 0; j < 4; ++j)
    {
        generateBox(level, chunkBB, offX+j,    j+1, j,    offX+j,    j+1, 20,   Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, offX+j+7,  j+5, j+7,  offX+j+7,  j+5, 20,   Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, offX+17-j, j+5, j+7,  offX+17-j, j+5, 20,   Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, offX+24-j, j+1, j,    offX+24-j, j+1, 20,   Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, offX+j+1,  j+1, j,    offX+23-j, j+1, j,    Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, offX+j+8,  j+5, j+7,  offX+16-j, j+5, j+7,  Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }

    generateBox(level, chunkBB, offX+4, 4, 4, offX+6, 4, 20, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, offX+7, 4, 4, offX+17, 4, 6, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, offX+18, 4, 4, offX+20, 4, 20, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, offX+11, 8, 11, offX+13, 8, 20, Tile::prismarine_Id, blockPrismarine(), false);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), offX + 12, 9, 12, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), offX + 12, 9, 15, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), offX + 12, 9, 18, chunkBB);

    int j1 = isRight ? offX+19 : offX+5;
    int k  = isRight ? offX+5  : offX+19;

    for (int l = 20; l >= 5; l -= 3)
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), j1, 5, l, chunkBB);
    for (int k1 = 19; k1 >= 7; k1 -= 3)
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), k, 5, k1, chunkBB);
    for (int l1 = 0; l1 < 4; ++l1)
    {
        int i1 = isRight ? offX + (24 - (17 - l1*3)) : offX + 17 - l1*3;
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 5, 5, chunkBB);
    }
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), k, 5, 5, chunkBB);

    generateBox(level, chunkBB, offX+11, 1, 12, offX+13, 7, 12, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, offX+12, 1, 11, offX+12, 7, 13, Tile::prismarine_Id, blockPrismarine(), false);
}

// func_175839_b
void OceanMonumentPieces::MonumentBuilding::buildEntrance(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (!intersectsXZ(chunkBB, 22, 5, 35, 17)) return;

    fillWithAirOrWater(level, chunkBB, 25, 0, 0, 32, 8, 20, false);
    for (int i = 0; i < 4; ++i)
    {
        generateBox(level, chunkBB, 24, 2, 5+i*4, 24, 4, 5+i*4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 22, 4, 5+i*4, 23, 4, 5+i*4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 25, 5, 5 + i * 4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 26, 6, 5 + i * 4, chunkBB);
        placeBlock(level, blockSeaLantern(),       0, 26, 5, 5+i*4, chunkBB);
        generateBox(level, chunkBB, 33, 2, 5+i*4, 33, 4, 5+i*4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 34, 4, 5+i*4, 35, 4, 5+i*4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 32, 5, 5 + i * 4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 31, 6, 5 + i * 4, chunkBB);
        placeBlock(level, blockSeaLantern(),       0, 31, 5, 5+i*4, chunkBB);
        generateBox(level, chunkBB, 27, 6, 5+i*4, 30, 6, 5+i*4, Tile::prismarine_Id, blockPrismarine(), false);
    }
}

// func_175837_c
void OceanMonumentPieces::MonumentBuilding::buildTopSection(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (!intersectsXZ(chunkBB, 15, 20, 42, 21)) return;

    generateBox(level, chunkBB, 15, 0, 21, 42, 0, 21, Tile::prismarine_Id, blockPrismarine(), false);
    fillWithAirOrWater(level, chunkBB, 26, 1, 21, 31, 3, 21, false);
    generateBox(level, chunkBB, 21, 12, 21, 36, 12, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 17, 11, 21, 40, 11, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 16, 10, 21, 41, 10, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 15,  7, 21, 42,  9, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 16,  6, 21, 41,  6, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 17,  5, 21, 40,  5, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 21,  4, 21, 36,  4, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 22,  3, 21, 26,  3, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 31,  3, 21, 35,  3, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 23,  2, 21, 25,  2, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 32,  2, 21, 34,  2, 21, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 28,  4, 20, 29,  4, 21, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 27, 3, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 26, 2, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 31, 2, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 25, 1, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 32, 1, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 30, 3, 21, chunkBB);

    for (int i = 0; i < 7; ++i) {
        placeBlock(level, Tile::prismarine_Id,blockDarkPrismarine(), 28-i, 6+i, 21, chunkBB);
        placeBlock(level, Tile::prismarine_Id,blockDarkPrismarine(), 29+i, 6+i, 21, chunkBB);
    }
    for (int j = 0; j < 4; ++j) {
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 28-j, 9+j, 21, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 29+j, 9+j, 21, chunkBB);
    }
    placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 28, 12, 21, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 29, 12, 21, chunkBB);

    for (int k = 0; k < 3; ++k) {
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 22-k*2,  8, 21, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 22-k*2,  9, 21, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 35+k*2,  8, 21, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockDarkPrismarine(), 35+k*2,  9, 21, chunkBB);
    }

    fillWithAirOrWater(level, chunkBB, 15, 13, 21, 42, 15, 21, false);
    fillWithAirOrWater(level, chunkBB, 15,  1, 21, 15,  6, 21, false);
    fillWithAirOrWater(level, chunkBB, 16,  1, 21, 16,  5, 21, false);
    fillWithAirOrWater(level, chunkBB, 17,  1, 21, 20,  4, 21, false);
    fillWithAirOrWater(level, chunkBB, 21,  1, 21, 21,  3, 21, false);
    fillWithAirOrWater(level, chunkBB, 22,  1, 21, 22,  2, 21, false);
    fillWithAirOrWater(level, chunkBB, 23,  1, 21, 24,  1, 21, false);
    fillWithAirOrWater(level, chunkBB, 42,  1, 21, 42,  6, 21, false);
    fillWithAirOrWater(level, chunkBB, 41,  1, 21, 41,  5, 21, false);
    fillWithAirOrWater(level, chunkBB, 37,  1, 21, 40,  4, 21, false);
    fillWithAirOrWater(level, chunkBB, 36,  1, 21, 36,  3, 21, false);
    fillWithAirOrWater(level, chunkBB, 33,  1, 21, 34,  1, 21, false);
    fillWithAirOrWater(level, chunkBB, 35,  1, 21, 35,  2, 21, false);
}

// func_175841_d
void OceanMonumentPieces::MonumentBuilding::buildCorridorCenter(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (!intersectsXZ(chunkBB, 21, 21, 36, 36)) return;

    generateBox(level, chunkBB, 21, 0, 22, 36, 0, 36, Tile::prismarine_Id, blockPrismarine(), false);
    fillWithAirOrWater(level, chunkBB, 21, 1, 22, 36, 23, 36, false);

    for (int i = 0; i < 4; ++i) {
        generateBox(level, chunkBB, 21+i, 13+i, 21+i, 36-i, 13+i, 21+i, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 21+i, 13+i, 36-i, 36-i, 13+i, 36-i, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 21+i, 13+i, 22+i, 21+i, 13+i, 35-i, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 36-i, 13+i, 22+i, 36-i, 13+i, 35-i, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }

    generateBox(level, chunkBB, 25, 16, 25, 32, 16, 32, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 25, 17, 25, 25, 19, 25, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 32, 17, 25, 32, 19, 25, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 25, 17, 32, 25, 19, 32, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 32, 17, 32, 32, 19, 32, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 26, 20, 26, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 27, 21, 27, chunkBB);
    placeBlock(level, blockSeaLantern(),       0, 27, 20, 27, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 26, 20, 31, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 27, 21, 30, chunkBB);
    placeBlock(level, blockSeaLantern(),       0, 27, 20, 30, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 31, 20, 31, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 30, 21, 30, chunkBB);
    placeBlock(level, blockSeaLantern(),       0, 30, 20, 30, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 31, 20, 26, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 30, 21, 27, chunkBB);
    placeBlock(level, blockSeaLantern(),       0, 30, 20, 27, chunkBB);
    generateBox(level, chunkBB, 28, 21, 27, 29, 21, 27, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 27, 21, 28, 27, 21, 29, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 28, 21, 30, 29, 21, 30, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 30, 21, 28, 30, 21, 29, Tile::prismarine_Id, blockPrismarine(), false);
}

// func_175835_e
void OceanMonumentPieces::MonumentBuilding::buildSideCorridor(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (intersectsXZ(chunkBB, 0, 21, 6, 58))
    {
        generateBox(level, chunkBB, 0, 0, 21, 6, 0, 57, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 0, 1, 21, 6, 7, 57, false);
        generateBox(level, chunkBB, 4, 4, 21, 6, 4, 53, Tile::prismarine_Id, blockPrismarine(), false);
        for (int i = 0; i < 4; ++i)
            generateBox(level, chunkBB, i, i+1, 21, i, i+1, 57-i, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int j = 23; j < 53; j += 3)
            placeBlock(level,Tile::prismarine_Id, blockPrismarineBricks(), 5, 5, j, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 5, 5, 52, chunkBB);
        generateBox(level, chunkBB, 4, 1, 52, 6, 3, 52, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 5, 1, 51, 5, 3, 53, Tile::prismarine_Id, blockPrismarine(), false);
    }
    if (intersectsXZ(chunkBB, 51, 21, 58, 58))
    {
        generateBox(level, chunkBB, 51, 0, 21, 57, 0, 57, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 51, 1, 21, 57, 7, 57, false);
        generateBox(level, chunkBB, 51, 4, 21, 53, 4, 53, Tile::prismarine_Id, blockPrismarine(), false);
        for (int l = 0; l < 4; ++l)
            generateBox(level, chunkBB, 57-l, l+1, 21, 57-l, l+1, 57-l, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int i1 = 23; i1 < 53; i1 += 3)
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 52, 5, i1, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 52, 5, 52, chunkBB);
        generateBox(level, chunkBB, 51, 1, 52, 53, 3, 52, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 52, 1, 51, 52, 3, 53, Tile::prismarine_Id, blockPrismarine(), false);
    }
    if (intersectsXZ(chunkBB, 0, 51, 57, 57))
    {
        generateBox(level, chunkBB, 7, 0, 51, 50, 0, 57, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 7, 1, 51, 50, 10, 57, false);
        for (int j1 = 0; j1 < 4; ++j1)
            generateBox(level, chunkBB, j1+1, j1+1, 57-j1, 56-j1, j1+1, 57-j1, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
}

// func_175842_f
void OceanMonumentPieces::MonumentBuilding::buildInnerCorridor(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (intersectsXZ(chunkBB, 7, 21, 13, 50))
    {
        generateBox(level, chunkBB, 7, 0, 21, 13, 0, 50, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 7, 1, 21, 13, 10, 50, false);
        generateBox(level, chunkBB, 11, 8, 21, 13, 8, 53, Tile::prismarine_Id, blockPrismarine(), false);
        for (int i = 0; i < 4; ++i)
            generateBox(level, chunkBB, i+7, i+5, 21, i+7, i+5, 54, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int j = 21; j <= 45; j += 3)
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 12, 9, j, chunkBB);
    }
    if (intersectsXZ(chunkBB, 44, 21, 50, 54))
    {
        generateBox(level, chunkBB, 44, 0, 21, 50, 0, 50, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 44, 1, 21, 50, 10, 50, false);
        generateBox(level, chunkBB, 44, 8, 21, 46, 8, 53, Tile::prismarine_Id, blockPrismarine(), false);
        for (int k = 0; k < 4; ++k)
            generateBox(level, chunkBB, 50-k, k+5, 21, 50-k, k+5, 54, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int l = 21; l <= 45; l += 3)
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 45, 9, l, chunkBB);
    }
    if (intersectsXZ(chunkBB, 8, 44, 49, 54))
    {
        generateBox(level, chunkBB, 14, 0, 44, 43, 0, 50, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 14, 1, 44, 43, 10, 50, false);
        for (int i1 = 12; i1 <= 45; i1 += 3)
        {
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 9, 45, chunkBB);
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 9, 52, chunkBB);
            if (i1==12||i1==18||i1==24||i1==33||i1==39||i1==45)
            {
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 9,  47, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 9,  50, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 10, 45, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 10, 46, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 10, 51, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 10, 52, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 11, 47, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 11, 50, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  i1, 12, 48, chunkBB);
                placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), i1, 12, 49, chunkBB);

            }
        }
        for (int j1 = 0; j1 < 3; ++j1)
        generateBox(level, chunkBB, 8+j1, 5+j1, 54, 49-j1, 5+j1, 54, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 11, 8, 54, 46, 8, 54, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 14, 8, 44, 43, 8, 53, Tile::prismarine_Id, blockPrismarine(), false);
    }
}

// func_175838_g
void OceanMonumentPieces::MonumentBuilding::buildUpperSection(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (intersectsXZ(chunkBB, 14, 21, 20, 43))
    {
        generateBox(level, chunkBB, 14, 0, 21, 20, 0, 43, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 14, 1, 22, 20, 14, 43, false);
        generateBox(level, chunkBB, 18, 12, 22, 20, 12, 39, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 18, 12, 21, 20, 12, 21, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int i = 0; i < 4; ++i)
            generateBox(level, chunkBB, i+14, i+9, 21, i+14, i+9, 43-i, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int j = 23; j <= 39; j += 3)
            placeBlock(level,Tile::prismarine_Id, blockPrismarineBricks(), 19, 13, j, chunkBB);
    }
    if (intersectsXZ(chunkBB, 37, 21, 43, 43))
    {
        generateBox(level, chunkBB, 37, 0, 21, 43, 0, 43, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 37, 1, 22, 43, 14, 43, false);
        generateBox(level, chunkBB, 37, 12, 22, 39, 12, 39, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 37, 12, 21, 39, 12, 21, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int k = 0; k < 4; ++k)
            generateBox(level, chunkBB, 43-k, k+9, 21, 43-k, k+9, 43-k, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(), false);
        for (int l = 23; l <= 39; l += 3)
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  38, 13, l, chunkBB);
    }
    if (intersectsXZ(chunkBB, 15, 37, 42, 43))
    {
        generateBox(level, chunkBB, 21, 0, 37, 36, 0, 43, Tile::prismarine_Id, blockPrismarine(), false);
        fillWithAirOrWater(level, chunkBB, 21, 1, 37, 36, 14, 43, false);
        generateBox(level, chunkBB, 21, 12, 37, 36, 12, 39, Tile::prismarine_Id, blockPrismarine(), false);
        for (int i1 = 0; i1 < 4; ++i1)
            generateBox(level, chunkBB, 15+i1, i1+9, 43-i1, 42-i1, i1+9, 43-i1, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        for (int j1 = 21; j1 <= 36; j1 += 3)
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), j1, 13, 38, chunkBB);
    }
}


OceanMonumentPieces::CoreRoom::CoreRoom() {}
OceanMonumentPieces::CoreRoom::CoreRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 2, 2, 2) {}

bool OceanMonumentPieces::CoreRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{

    fillWaterWith(level, chunkBB, 1, 8, 0, 14, 8, 14,
                  Tile::prismarine_Id);

    // Pareti y=7
    int i = 7;
    generateBox(level, chunkBB,  0, i,  0,  0, i, 15, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 15, i,  0, 15, i, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  1, i,  0, 15, i,  0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  1, i, 15, 14, i, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);

    for (i = 1; i <= 6; ++i)
    {

        int meta = (i == 2 || i == 6) ? blockPrismarine() : blockPrismarineBricks();

        for (int j = 0; j <= 15; j += 15)
        {

            generateBox(level, chunkBB, j, i, 0, j, i, 1,   Tile::prismarine_Id, meta, Tile::prismarine_Id, meta, false);
            generateBox(level, chunkBB, j, i, 6, j, i, 9,   Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
            generateBox(level, chunkBB, j, i,14, j, i,15,   Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        }
        generateBox(level, chunkBB,  1, i,  0,  1, i,  0, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB,  6, i,  0,  9, i,  0, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB, 14, i,  0, 14, i,  0, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB,  1, i, 15, 14, i, 15, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
    }


    generateBox(level, chunkBB, 6, 3, 6, 9, 6, 9, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(), false);

    generateBox(level, chunkBB, 7, 4, 7, 8, 5, 8, blockGoldBlock(), 0, false);

    for (i = 3; i <= 6; i += 3)
    {
        for (int k = 6; k <= 9; k += 3)
        {
            placeBlock(level, blockSeaLantern(), 0, k, i, 6, chunkBB);
            placeBlock(level, blockSeaLantern(), 0, k, i, 9, chunkBB);
        }
    }


    generateBox(level, chunkBB,  5, 1,  6,  5, 2,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 1,  9,  5, 2,  9, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 1,  6, 10, 2,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 1,  9, 10, 2,  9, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  6, 1,  5,  6, 2,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  9, 1,  5,  9, 2,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  6, 1, 10,  6, 2, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  9, 1, 10,  9, 2, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 2,  5,  5, 6,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 2, 10,  5, 6, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 2,  5, 10, 6,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 2, 10, 10, 6, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 7,  1,  5, 7,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 7,  1, 10, 7,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 7,  9,  5, 7, 14, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 7,  9, 10, 7, 14, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 7,  5,  6, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 7, 10,  6, 7, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  9, 7,  5, 14, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  9, 7, 10, 14, 7, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  2, 1,  2,  2, 1,  3, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  3, 1,  2,  3, 1,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 13, 1,  2, 13, 1,  3, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 12, 1,  2, 12, 1,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  2, 1, 12,  2, 1, 13, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  3, 1, 13,  3, 1, 13, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 13, 1, 12, 13, 1, 13, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 12, 1, 13, 12, 1, 13, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

    return true;
}


OceanMonumentPieces::SimpleRoom::SimpleRoom() : variant(0) {}
OceanMonumentPieces::SimpleRoom::SimpleRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 1, 1, 1)
{
    variant = random->nextInt(3);
}

bool OceanMonumentPieces::SimpleRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (roomDef->index / 25 > 0)
        generateFloor(level, chunkBB, 0, 0, roomDef->hasOpening[F_DOWN]);

    if (roomDef->neighbors[F_UP] == nullptr)
        fillWaterWith(level, chunkBB, 1, 4, 1, 6, 4, 6, Tile::prismarine_Id);

    bool placeChest = variant != 0 && random->nextBoolean() &&
                      !roomDef->hasOpening[F_DOWN] && !roomDef->hasOpening[F_UP] &&
                      roomDef->countOpenings() > 1;

    if (variant == 0)
    {

        generateBox(level, chunkBB, 0, 1, 0, 2, 1, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 3, 0, 2, 3, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 2, 0, 0, 2, 2, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 1, 2, 0, 2, 2, 0, Tile::prismarine_Id, blockPrismarine(), false);
        placeBlock(level, blockSeaLantern(), 0, 1, 2, 1, chunkBB);
        generateBox(level, chunkBB, 5, 1, 0, 7, 1, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 5, 3, 0, 7, 3, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 7, 2, 0, 7, 2, 2, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 5, 2, 0, 6, 2, 0, Tile::prismarine_Id, blockPrismarine(), false);
        placeBlock(level, blockSeaLantern(), 0, 6, 2, 1, chunkBB);
        generateBox(level, chunkBB, 0, 1, 5, 2, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 3, 5, 2, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 2, 5, 0, 2, 7, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 1, 2, 7, 2, 2, 7, Tile::prismarine_Id, blockPrismarine(), false);
        placeBlock(level, blockSeaLantern(), 0, 1, 2, 6, chunkBB);
        generateBox(level, chunkBB, 5, 1, 5, 7, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 5, 3, 5, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 7, 2, 5, 7, 2, 7, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 5, 2, 7, 6, 2, 7, Tile::prismarine_Id, blockPrismarine(), false);
        placeBlock(level, blockSeaLantern(), 0, 6, 2, 6, chunkBB);

        if (roomDef->hasOpening[F_SOUTH]) { generateBox(level, chunkBB, 3, 3, 0, 4, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
        else {
            generateBox(level, chunkBB, 3, 3, 0, 4, 3, 1, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 3, 2, 0, 4, 2, 0, Tile::prismarine_Id, blockPrismarine(), false);
            generateBox(level, chunkBB, 3, 1, 0, 4, 1, 1, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        }
        if (roomDef->hasOpening[F_NORTH]) { generateBox(level, chunkBB, 3, 3, 7, 4, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
        else {
            generateBox(level, chunkBB, 3, 3, 6, 4, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 3, 2, 7, 4, 2, 7, Tile::prismarine_Id, blockPrismarine(), false);
            generateBox(level, chunkBB, 3, 1, 6, 4, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        }
        if (roomDef->hasOpening[F_WEST]) { generateBox(level, chunkBB, 0, 3, 3, 0, 3, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false); }
        else {
            generateBox(level, chunkBB, 0, 3, 3, 1, 3, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 0, 2, 3, 0, 2, 4, Tile::prismarine_Id, blockPrismarine(), false);
            generateBox(level, chunkBB, 0, 1, 3, 1, 1, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        }
        if (roomDef->hasOpening[F_EAST]) { generateBox(level, chunkBB, 7, 3, 3, 7, 3, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false); }
        else {
            generateBox(level, chunkBB, 6, 3, 3, 7, 3, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 7, 2, 3, 7, 2, 4, Tile::prismarine_Id, blockPrismarine(), false);
            generateBox(level, chunkBB, 6, 1, 3, 7, 1, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        }
    }
    else if (variant == 1)
    {
        generateBox(level, chunkBB, 2, 1, 2, 2, 3, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 2, 1, 5, 2, 3, 5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 5, 1, 5, 5, 3, 5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 5, 1, 2, 5, 3, 2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        placeBlock(level, blockSeaLantern(), 0, 2, 2, 2, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 2, 2, 5, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 5, 2, 5, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 5, 2, 2, chunkBB);
        generateBox(level, chunkBB, 0, 1, 0, 1, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 1, 1, 0, 3, 1, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 0, 1, 7, 1, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 1, 6, 0, 3, 6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 6, 1, 7, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 7, 1, 6, 7, 3, 6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 6, 1, 0, 7, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 7, 1, 1, 7, 3, 1, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 1, 2, 0, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 0, 2, 1, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 1, 2, 7, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 0, 2, 6, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 6, 2, 7, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 7, 2, 6, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 6, 2, 0, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarine(), 7, 2, 1, chunkBB);

        if (!roomDef->hasOpening[F_SOUTH]) { generateBox(level, chunkBB, 1, 3, 0, 6, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); generateBox(level, chunkBB, 1, 2, 0, 6, 2, 0, Tile::prismarine_Id, blockPrismarine(), false); generateBox(level, chunkBB, 1, 1, 0, 6, 1, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
        if (!roomDef->hasOpening[F_NORTH]) { generateBox(level, chunkBB, 1, 3, 7, 6, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false); generateBox(level, chunkBB, 1, 2, 7, 6, 2, 7, Tile::prismarine_Id, blockPrismarine(), false); generateBox(level, chunkBB, 1, 1, 7, 6, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
        if (!roomDef->hasOpening[F_WEST])  { generateBox(level, chunkBB, 0, 3, 1, 0, 3, 6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false); generateBox(level, chunkBB, 0, 2, 1, 0, 2, 6, Tile::prismarine_Id, blockPrismarine(), false); generateBox(level, chunkBB, 0, 1, 1, 0, 1, 6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
        if (!roomDef->hasOpening[F_EAST])  { generateBox(level, chunkBB, 7, 3, 1, 7, 3, 6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false); generateBox(level, chunkBB, 7, 2, 1, 7, 2, 6, Tile::prismarine_Id, blockPrismarine(), false); generateBox(level, chunkBB, 7, 1, 1, 7, 1, 6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false); }
    }
    else // variant == 2
    {
        generateBox(level, chunkBB, 0, 1, 0, 0, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 7, 1, 0, 7, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1, 0, 6, 1, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1, 7, 6, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 0, 2, 0, 0, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 7, 2, 0, 7, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 1, 2, 0, 6, 2, 0, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 1, 2, 7, 6, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 0, 3, 0, 0, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 7, 3, 0, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 1, 3, 0, 6, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 3, 7, 6, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 0, 1, 3, 0, 2, 4, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 7, 1, 3, 7, 2, 4, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 3, 1, 0, 4, 2, 0, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        generateBox(level, chunkBB, 3, 1, 7, 4, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(),  false);
        if (roomDef->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 0, 4, 2, 0, false);
        if (roomDef->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 7, 4, 2, 7, false);
        if (roomDef->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1, 3, 0, 2, 4, false);
        if (roomDef->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 7, 1, 3, 7, 2, 4, false);
    }

    if (placeChest)
    {
        generateBox(level, chunkBB, 3, 1, 3, 4, 1, 4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 3, 2, 3, 4, 2, 4, Tile::prismarine_Id, blockPrismarine(), false);
        generateBox(level, chunkBB, 3, 3, 3, 4, 3, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
    return true;
}


OceanMonumentPieces::SimpleTopRoom::SimpleTopRoom() {}
OceanMonumentPieces::SimpleTopRoom::SimpleTopRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 1, 1, 1) {}

bool OceanMonumentPieces::SimpleTopRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (roomDef->index / 25 > 0)
        generateFloor(level, chunkBB, 0, 0, roomDef->hasOpening[F_DOWN]);

    if (roomDef->neighbors[F_UP] == nullptr)
        fillWaterWith(level, chunkBB, 1, 4, 1, 6, 4, 6,
                      Tile::prismarine_Id);


    for (int i = 1; i <= 6; ++i)
    for (int j = 1; j <= 6; ++j)
    {
        if (random->nextInt(3) != 0)
        {
            int k = 2 + (random->nextInt(4) == 0 ? 0 : 1);
            generateBox(level, chunkBB, i, k, j, i, 3, j, Tile::sponge_Id, 1, Tile::sponge_Id, 1, false);
        }
    }

    generateBox(level, chunkBB, 0, 1, 0, 0, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 7, 1, 0, 7, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1, 0, 6, 1, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1, 7, 6, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 0, 2, 0, 0, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(),false);
    generateBox(level, chunkBB, 7, 2, 0, 7, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 1, 2, 0, 6, 2, 0, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 1, 2, 7, 6, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 0, 3, 0, 0, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 7, 3, 0, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 3, 0, 6, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 3, 7, 6, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 0, 1, 3, 0, 2, 4, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 7, 1, 3, 7, 2, 4, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 3, 1, 0, 4, 2, 0, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(),false);
    generateBox(level, chunkBB, 3, 1, 7, 4, 2, 7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    if (roomDef->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 0, 4, 2, 0, false);

    return true;
}


// DoubleXRoom

OceanMonumentPieces::DoubleXRoom::DoubleXRoom() {}
OceanMonumentPieces::DoubleXRoom::DoubleXRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 2, 1, 1) {}

bool OceanMonumentPieces::DoubleXRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    RoomDefinition* eastRoom = roomDef->neighbors[F_EAST];
    RoomDefinition* thisRoom = roomDef;

    if (roomDef->index / 25 > 0)
    {
        generateFloor(level, chunkBB, 8, 0, eastRoom->hasOpening[F_DOWN]);
        generateFloor(level, chunkBB, 0, 0, thisRoom->hasOpening[F_DOWN]);
    }


    if (thisRoom->neighbors[F_UP] == nullptr)
        fillWaterWith(level, chunkBB, 1, 4, 1, 7, 4, 6, Tile::prismarine_Id);
    if (eastRoom->neighbors[F_UP] == nullptr)
        fillWaterWith(level, chunkBB, 8, 4, 1, 14, 4, 6, Tile::prismarine_Id);


    generateBox(level, chunkBB,  0, 3,  0,  0, 3,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 15, 3,  0, 15, 3,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 3,  0, 15, 3,  0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 3,  7, 14, 3,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

    generateBox(level, chunkBB,  0, 2,  0,  0, 2,  7, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 15, 2,  0, 15, 2,  7, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  1, 2,  0, 15, 2,  0, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  1, 2,  7, 14, 2,  7, Tile::prismarine_Id, blockPrismarine(), false);

    generateBox(level, chunkBB,  0, 1,  0,  0, 1,  7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 15, 1,  0, 15, 1,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 1,  0, 15, 1,  0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  1, 1,  7, 14, 1,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

    generateBox(level, chunkBB,  5, 1,  0, 10, 1,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  6, 2,  0,  9, 2,  3, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  5, 3,  0, 10, 3,  4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);


    placeBlock(level, blockSeaLantern(), 0, 6, 2, 3, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 9, 2, 3, chunkBB);

    if (thisRoom->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB,  3, 1,  0,  4, 2,  0, false);
    if (thisRoom->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB,  3, 1,  7,  4, 2,  7, false);
    if (thisRoom->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB,  0, 1,  3,  0, 2,  4, false);
    if (eastRoom->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 11, 1,  0, 12, 2,  0, false);
    if (eastRoom->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 11, 1,  7, 12, 2,  7, false);
    if (eastRoom->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 15, 1,  3, 15, 2,  4, false);

    return true;
}


// DoubleXYRoom

OceanMonumentPieces::DoubleXYRoom::DoubleXYRoom() {}
OceanMonumentPieces::DoubleXYRoom::DoubleXYRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 2, 2, 1) {}

bool OceanMonumentPieces::DoubleXYRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    RoomDefinition* r0 = roomDef;
    RoomDefinition* r1 = roomDef->neighbors[F_EAST];
    RoomDefinition* r2 = r0->neighbors[F_UP];
    RoomDefinition* r3 = r1->neighbors[F_UP];

    if (roomDef->index / 25 > 0)
    {
        generateFloor(level, chunkBB, 8, 0, r1->hasOpening[F_DOWN]);
        generateFloor(level, chunkBB, 0, 0, r0->hasOpening[F_DOWN]);
    }


    if (r2->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB,  1, 8, 1,  7, 8, 6, Tile::prismarine_Id);
    if (r3->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB,  8, 8, 1, 14, 8, 6, Tile::prismarine_Id);

    for (int i = 1; i <= 7; ++i)
    {

        int meta = (i == 2 || i == 6) ? blockPrismarine() : blockPrismarineBricks();
        generateBox(level, chunkBB,  0, i,  0,  0, i,  7, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB, 15, i,  0, 15, i,  7, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB,  1, i,  0, 15, i,  0, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
        generateBox(level, chunkBB,  1, i,  7, 14, i,  7, Tile::prismarine_Id, meta, Tile::prismarine_Id, meta,false);
    }


    generateBox(level, chunkBB,  2, 1,  3,  2, 7,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  3, 1,  2,  4, 7,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  3, 1,  5,  4, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 13, 1,  3, 13, 7,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 11, 1,  2, 12, 7,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 11, 1,  5, 12, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 1,  3,  5, 3,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 1,  3, 10, 3,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 7,  2, 10, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 5,  2,  5, 7,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 5,  2, 10, 7,  2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  5, 5,  5,  5, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 10, 5,  5, 10, 7,  5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);


    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  6, 6, 2, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  9, 6, 2, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  6, 6, 5, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  9, 6, 5, chunkBB);

    generateBox(level, chunkBB,  5, 4,  3,  6, 4,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  9, 4,  3, 10, 4,  4, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);


    placeBlock(level, blockSeaLantern(), 0,  5, 4, 2, chunkBB);
    placeBlock(level, blockSeaLantern(), 0,  5, 4, 5, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 10, 4, 2, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 10, 4, 5, chunkBB);

    if (r0->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB,  3, 1,  0,  4, 2,  0, false);
    if (r0->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB,  3, 1,  7,  4, 2,  7, false);
    if (r0->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB,  0, 1,  3,  0, 2,  4, false);
    if (r1->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 11, 1,  0, 12, 2,  0, false);
    if (r1->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 11, 1,  7, 12, 2,  7, false);
    if (r1->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 15, 1,  3, 15, 2,  4, false);
    if (r2->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB,  3, 5,  0,  4, 6,  0, false);
    if (r2->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB,  3, 5,  7,  4, 6,  7, false);
    if (r2->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB,  0, 5,  3,  0, 6,  4, false);
    if (r3->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 11, 5,  0, 12, 6,  0, false);
    if (r3->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 11, 5,  7, 12, 6,  7, false);
    if (r3->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 15, 5,  3, 15, 6,  4, false);

    return true;
}


// DoubleYRoom

OceanMonumentPieces::DoubleYRoom::DoubleYRoom() {}
OceanMonumentPieces::DoubleYRoom::DoubleYRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 1, 2, 1) {}

bool OceanMonumentPieces::DoubleYRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (roomDef->index / 25 > 0)
        generateFloor(level, chunkBB, 0, 0, roomDef->hasOpening[F_DOWN]);

    RoomDefinition* upRoom = roomDef->neighbors[F_UP];

    if (upRoom->neighbors[F_UP] == nullptr)
        fillWaterWith(level, chunkBB, 1, 8, 1, 6, 8, 6, Tile::prismarine_Id);


    generateBox(level, chunkBB, 0, 4, 0, 0, 4, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 7, 4, 0, 7, 4, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 4, 0, 6, 4, 0, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 4, 7, 6, 4, 7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 2, 4, 1, 2, 4, 2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 4, 2, 1, 4, 2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 5, 4, 1, 5, 4, 2, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 6, 4, 2, 6, 4, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 2, 4, 5, 2, 4, 6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 1, 4, 5, 1, 4, 5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 5, 4, 5, 5, 4, 6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 6, 4, 5, 6, 4, 5, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

    RoomDefinition* cur = roomDef;
    for (int i = 1; i <= 5; i += 4)
    {
        int j = 0;
        if (cur->hasOpening[F_SOUTH]) {
            generateBox(level, chunkBB, 2, i, j, 2, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 5, i, j, 5, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 3, i+2, j, 4, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        } else {
            generateBox(level, chunkBB, 0, i, j, 7, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 0, i+1, j, 7, i+1, j, Tile::prismarine_Id, blockPrismarine(), false);
        }

        j = 7;
        if (cur->hasOpening[F_NORTH]) {
            generateBox(level, chunkBB, 2, i, j, 2, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 5, i, j, 5, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 3, i+2, j, 4, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        } else {
            generateBox(level, chunkBB, 0, i, j, 7, i+2, j, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, 0, i+1, j, 7, i+1, j, Tile::prismarine_Id, blockPrismarine(), false);
        }

        int k = 0;
        if (cur->hasOpening[F_WEST]) {
            generateBox(level, chunkBB, k, i, 2, k, i+2, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i, 5, k, i+2, 5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i+2, 3, k, i+2, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        } else {
            generateBox(level, chunkBB, k, i, 0, k, i+2, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i+1, 0, k, i+1, 7, Tile::prismarine_Id, blockPrismarine(), false);
        }

        k = 7;
        if (cur->hasOpening[F_EAST]) {
            generateBox(level, chunkBB, k, i, 2, k, i+2, 2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i, 5, k, i+2, 5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i+2, 3, k, i+2, 4, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        } else {
            generateBox(level, chunkBB, k, i, 0, k, i+2, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, k, i+1, 0, k, i+1, 7, Tile::prismarine_Id, blockPrismarine(), false);
        }

        cur = upRoom;
    }
    return true;
}


// DoubleYZRoom

OceanMonumentPieces::DoubleYZRoom::DoubleYZRoom() {}
OceanMonumentPieces::DoubleYZRoom::DoubleYZRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 1, 2, 2) {}

bool OceanMonumentPieces::DoubleYZRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    RoomDefinition* r0 = roomDef;
    RoomDefinition* r1 = roomDef->neighbors[F_NORTH];
    RoomDefinition* r2 = r1->neighbors[F_UP];
    RoomDefinition* r3 = r0->neighbors[F_UP];

    if (roomDef->index / 25 > 0)
    {
        generateFloor(level, chunkBB, 0, 8, r1->hasOpening[F_DOWN]);
        generateFloor(level, chunkBB, 0, 0, r0->hasOpening[F_DOWN]);
    }


    if (r3->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB, 1, 8,  1, 6, 8,  7, Tile::prismarine_Id);
    if (r2->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB, 1, 8,  8, 6, 8, 14, Tile::prismarine_Id);

    for (int i = 1; i <= 7; ++i)
    {

        int meta = (i == 2 || i == 6) ? blockPrismarine() : blockPrismarineBricks();
        generateBox(level, chunkBB, 0, i,  0, 0, i, 15, Tile::prismarine_Id, meta,Tile::prismarine_Id, meta, false);
        generateBox(level, chunkBB, 7, i,  0, 7, i, 15, Tile::prismarine_Id, meta,Tile::prismarine_Id, meta, false);
        generateBox(level, chunkBB, 1, i,  0, 6, i,  0, Tile::prismarine_Id, meta,Tile::prismarine_Id, meta, false);
        generateBox(level, chunkBB, 1, i, 15, 6, i, 15, Tile::prismarine_Id, meta,Tile::prismarine_Id, meta, false);
    }

    for (int j = 1; j <= 7; ++j)
    {

        int id   = (j == 2 || j == 6) ? blockSeaLantern() : Tile::prismarine_Id;
        int meta = (j == 2 || j == 6) ? 0                 : blockDarkPrismarine();

        generateBox(level, chunkBB, 3, j, 7, 4, j, 8, id, meta,id, meta, false);
    }

    if (r0->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 3, 1,  0, 4, 2,  0, false);
    if (r0->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 7, 1,  3, 7, 2,  4, false);
    if (r0->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1,  3, 0, 2,  4, false);
    if (r1->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 15, 4, 2, 15, false);
    if (r1->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1, 11, 0, 2, 12, false);
    if (r1->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 7, 1, 11, 7, 2, 12, false);
    if (r3->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 3, 5,  0, 4, 6,  0, false);

    // Corretti tutti i generateBox per usare Tile::prismarine_Id + blockPrismarineBricks()
    if (r3->hasOpening[F_EAST])  {
        fillWithAirOrWater(level, chunkBB, 7, 5,  3, 7, 6,  4, false);
        generateBox(level, chunkBB, 5, 4,  2, 6, 4,  5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 6, 1,  2, 6, 3,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 6, 1,  5, 6, 3,  5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
    if (r3->hasOpening[F_WEST])  {
        fillWithAirOrWater(level, chunkBB, 0, 5,  3, 0, 6,  4, false);
        generateBox(level, chunkBB, 1, 4,  2, 2, 4,  5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1,  2, 1, 3,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1,  5, 1, 3,  5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
    if (r2->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 3, 5, 15, 4, 6, 15, false);
    if (r2->hasOpening[F_WEST])  {
        fillWithAirOrWater(level, chunkBB, 0, 5, 11, 0, 6, 12, false);
        generateBox(level, chunkBB, 1, 4, 10, 2, 4, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1, 10, 1, 3, 10, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 1, 1, 13, 1, 3, 13, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    }
    if (r2->hasOpening[F_EAST])  {
        fillWithAirOrWater(level, chunkBB, 7, 5, 11, 7, 6, 12, false);
        generateBox(level, chunkBB, 5, 4, 10, 6, 4, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 6, 1, 10, 6, 3, 10, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 6, 1, 13, 6, 3, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    }
    return true;
}


// DoubleZRoom

OceanMonumentPieces::DoubleZRoom::DoubleZRoom() {}
OceanMonumentPieces::DoubleZRoom::DoubleZRoom(int facing, RoomDefinition* room, Random* random)
    : Piece(1, facing, room, 1, 1, 2) {}

bool OceanMonumentPieces::DoubleZRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    RoomDefinition* r0 = roomDef;
    RoomDefinition* r1 = roomDef->neighbors[F_NORTH];

    if (roomDef->index / 25 > 0)
    {
        generateFloor(level, chunkBB, 0, 8, r1->hasOpening[F_DOWN]);
        generateFloor(level, chunkBB, 0, 0, r0->hasOpening[F_DOWN]);
    }

    // Corretto fillWaterWith
    if (r0->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB, 1, 4,  1, 6, 4,  7, Tile::prismarine_Id);
    if (r1->neighbors[F_UP] == nullptr) fillWaterWith(level, chunkBB, 1, 4,  8, 6, 4, 14, Tile::prismarine_Id);

    // Corretti i generateBox (ID, Metadato)
    generateBox(level, chunkBB, 0, 3,  0, 0, 3, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 7, 3,  0, 7, 3, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 3,  0, 7, 3,  0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 3, 15, 6, 3, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 0, 2,  0, 0, 2, 15, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 7, 2,  0, 7, 2, 15, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 1, 2,  0, 7, 2,  0, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 1, 2, 15, 6, 2, 15, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 0, 1,  0, 0, 1, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 7, 1,  0, 7, 1, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1,  0, 7, 1,  0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1, 15, 6, 1, 15, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1,  1, 1, 1,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 6, 1,  1, 6, 1,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 3,  1, 1, 3,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 6, 3,  1, 6, 3,  2, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1, 13, 1, 1, 14, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 6, 1, 13, 6, 1, 14, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 3, 13, 1, 3, 14, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 6, 3, 13, 6, 3, 14, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 2, 1,  6, 2, 3,  6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 5, 1,  6, 5, 3,  6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 2, 1,  9, 2, 3,  9, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 5, 1,  9, 5, 3,  9, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 3, 2,  6, 4, 2,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 3, 2,  9, 4, 2,  9, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB, 2, 2,  7, 2, 2,  8, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 5, 2,  7, 5, 2,  8, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);

    // Lanterne a posto
    placeBlock(level, blockSeaLantern(), 0, 2, 2,  5, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 5, 2,  5, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 2, 2, 10, chunkBB);
    placeBlock(level, blockSeaLantern(), 0, 5, 2, 10, chunkBB);

    // Corretti i placeBlock dei mattoni
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 2, 3,  5, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 5, 3,  5, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 2, 3, 10, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 5, 3, 10, chunkBB);

    if (r0->hasOpening[F_SOUTH]) fillWithAirOrWater(level, chunkBB, 3, 1,  0, 4, 2,  0, false);
    if (r0->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 7, 1,  3, 7, 2,  4, false);
    if (r0->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1,  3, 0, 2,  4, false);
    if (r1->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 15, 4, 2, 15, false);
    if (r1->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1, 11, 0, 2, 12, false);
    if (r1->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 7, 1, 11, 7, 2, 12, false);
    return true;
}

// EntryRoom

OceanMonumentPieces::EntryRoom::EntryRoom() {}
OceanMonumentPieces::EntryRoom::EntryRoom(int facing, RoomDefinition* room)
    : Piece(1, facing, room, 1, 1, 1) {}

bool OceanMonumentPieces::EntryRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    generateBox(level, chunkBB, 0, 3, 0, 2, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 5, 3, 0, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 6, 2, 0, 7, 2, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 0, 2, 0, 1, 2, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 0, 1, 0, 0, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 7, 1, 0, 7, 1, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 0, 1, 7, 7, 3, 7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 1, 1, 0, 2, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 5, 1, 0, 6, 3, 0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);

    if (roomDef->hasOpening[F_NORTH]) fillWithAirOrWater(level, chunkBB, 3, 1, 7, 4, 2, 7, false);
    if (roomDef->hasOpening[F_WEST])  fillWithAirOrWater(level, chunkBB, 0, 1, 3, 1, 2, 4, false);
    if (roomDef->hasOpening[F_EAST])  fillWithAirOrWater(level, chunkBB, 6, 1, 3, 7, 2, 4, false);
    return true;
}

// Penthouse

OceanMonumentPieces::Penthouse::Penthouse() {}
OceanMonumentPieces::Penthouse::Penthouse(int facing, BoundingBox* box)
    : Piece(facing, new BoundingBox(*box)) {}

bool OceanMonumentPieces::Penthouse::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    generateBox(level, chunkBB,  2, -1,  2, 11, -1, 11, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  0, -1,  0,  1, -1, 11, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB, 12, -1,  0, 13, -1, 11, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  2, -1,  0, 11, -1,  1, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  2, -1, 12, 11, -1, 13, Tile::prismarine_Id, blockPrismarine(), false);
    generateBox(level, chunkBB,  0,  0,  0,  0,  0, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB, 13,  0,  0, 13,  0, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  1,  0,  0, 12,  0,  0, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  1,  0, 13, 12,  0, 13, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);

    for (int i = 2; i <= 11; i += 3)
    {
        placeBlock(level, blockSeaLantern(), 0,  0, 0, i, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 13, 0, i, chunkBB);
        placeBlock(level, blockSeaLantern(), 0,  i, 0, 0, chunkBB);
    }

    generateBox(level, chunkBB,  2, 0,  3,  4, 0,  9, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  9, 0,  3, 11, 0,  9, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  4, 0,  9,  9, 0, 11, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);


    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  5, 0, 8, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  8, 0, 8, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 10, 0,10, chunkBB);
    placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  3, 0,10, chunkBB);

    generateBox(level, chunkBB,  3, 0,  3,  3, 0,  7, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB, 10, 0,  3, 10, 0,  7, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(), false);
    generateBox(level, chunkBB,  6, 0, 10,  7, 0, 10, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(), false);

    int col = 3;
    for (int j = 0; j < 2; ++j)
    {
        for (int k = 2; k <= 8; k += 3)
            generateBox(level, chunkBB, col, 0, k, col, 2, k, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        col = 10;
    }
    generateBox(level, chunkBB,  5, 0, 10,  5, 2, 10, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
    generateBox(level, chunkBB,  8, 0, 10,  8, 2, 10, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
    generateBox(level, chunkBB,  6, -1,  7,  7, -1,  8, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
    fillWithAirOrWater(level, chunkBB,  6, -1,  3,  7, -1,  4, false);
    spawnElderGuardian(level, chunkBB, 6, 1, 6);
    return true;
}


// WingRoom

OceanMonumentPieces::WingRoom::WingRoom() : wingType(0) {}
OceanMonumentPieces::WingRoom::WingRoom(int facing, BoundingBox* box, int seed)
    : Piece(facing, new BoundingBox(*box))
{
    wingType = seed & 1; // field_175834_o = p_i45585_3_ & 1
}

bool OceanMonumentPieces::WingRoom::postProcess(Level* level, Random* random, BoundingBox* chunkBB)
{
    if (wingType == 0)
    {
        for (int i = 0; i < 4; ++i)
            generateBox(level, chunkBB, 10-i, 3-i, 20-i, 12+i, 3-i, 20, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);

        generateBox(level, chunkBB,  7, 0,  6, 15, 0, 16, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB,  6, 0,  6,  6, 3, 20, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 16, 0,  6, 16, 3, 20, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB,  7, 1,  7,  7, 1, 20, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 15, 1,  7, 15, 1, 20, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB,  7, 1,  6,  9, 3,  6, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 13, 1,  6, 15, 3,  6, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB,  8, 1,  7,  9, 1,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 13, 1,  7, 14, 1,  7, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB,  9, 0,  5, 13, 0,  5, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 10, 0,  7, 12, 0,  7, Tile::prismarine_Id, blockDarkPrismarine(),Tile::prismarine_Id, blockDarkPrismarine(), false);
        generateBox(level, chunkBB,  8, 0, 10,  8, 0, 12, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(),false);
        generateBox(level, chunkBB, 14, 0, 10, 14, 0, 12, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockDarkPrismarine(),false);

        for (int i1 = 18; i1 >= 7; i1 -= 3)
        {
            placeBlock(level, blockSeaLantern(), 0,  6, 3, i1, chunkBB);
            placeBlock(level, blockSeaLantern(), 0, 16, 3, i1, chunkBB);
        }
        placeBlock(level, blockSeaLantern(), 0, 10, 0, 10, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 12, 0, 10, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 10, 0, 12, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 12, 0, 12, chunkBB);
        placeBlock(level, blockSeaLantern(), 0,  8, 3,  6, chunkBB);
        placeBlock(level, blockSeaLantern(), 0, 14, 3,  6, chunkBB);


        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  4, 2,  4, chunkBB);
        placeBlock(level, blockSeaLantern(),        0,  4, 1,  4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  4, 0,  4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 18, 2,  4, chunkBB);
        placeBlock(level, blockSeaLantern(),        0, 18, 1,  4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 18, 0,  4, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  4, 2, 18, chunkBB);
        placeBlock(level, blockSeaLantern(),        0,  4, 1, 18, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  4, 0, 18, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 18, 2, 18, chunkBB);
        placeBlock(level, blockSeaLantern(),        0, 18, 1, 18, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 18, 0, 18, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(),  9, 7, 20, chunkBB);
        placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), 13, 7, 20, chunkBB);

        generateBox(level, chunkBB,  6, 0, 21,  7, 4, 21, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 15, 0, 21, 16, 4, 21, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        spawnElderGuardian(level, chunkBB, 11, 2, 16);
    }
    else // wingType == 1
    {
        generateBox(level, chunkBB,  9, 3, 18, 13, 3, 20, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB,  9, 0, 18,  9, 2, 18, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
        generateBox(level, chunkBB, 13, 0, 18, 13, 2, 18, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);

        int j1 = 9;
        for (int l = 0; l < 2; ++l)
        {
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), j1, 6, 20, chunkBB);
            placeBlock(level, blockSeaLantern(),        0, j1, 5, 20, chunkBB);
            placeBlock(level, Tile::prismarine_Id, blockPrismarineBricks(), j1, 4, 20, chunkBB);
            j1 = 13;
        }

        generateBox(level, chunkBB,  7, 3,  7, 15, 3, 14, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        j1 = 10;
        for (int k1 = 0; k1 < 2; ++k1)
        {
            generateBox(level, chunkBB, j1, 0, 10, j1, 6, 10, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            generateBox(level, chunkBB, j1, 0, 12, j1, 6, 12, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            placeBlock(level, blockSeaLantern(), 0, j1, 0, 10, chunkBB);
            placeBlock(level, blockSeaLantern(), 0, j1, 0, 12, chunkBB);
            placeBlock(level, blockSeaLantern(), 0, j1, 4, 10, chunkBB);
            placeBlock(level, blockSeaLantern(), 0, j1, 4, 12, chunkBB);
            j1 = 12;
        }

        j1 = 8;
        for (int l1 = 0; l1 < 2; ++l1)
        {
            generateBox(level, chunkBB, j1, 0,  7, j1, 2,  7, Tile::prismarine_Id, blockPrismarineBricks(), Tile::prismarine_Id, blockPrismarineBricks(),false);
            generateBox(level, chunkBB, j1, 0, 14, j1, 2, 14, Tile::prismarine_Id, blockPrismarineBricks(),Tile::prismarine_Id, blockPrismarineBricks(), false);
            j1 = 14;
        }

        generateBox(level, chunkBB,  8, 3,  8,  8, 3, 13, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        generateBox(level, chunkBB, 14, 3,  8, 14, 3, 13, Tile::prismarine_Id, blockDarkPrismarine(), Tile::prismarine_Id, blockPrismarineBricks(),false);
        spawnElderGuardian(level, chunkBB, 11, 5, 13);
    }
    return true;
}
static int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

static Random populationRandom(long long seed, int chunkX, int chunkZ) {
    Random random(seed);
    long long first = random.nextLong();
    long long second = random.nextLong();
    uint64_t mulX = (uint64_t)(first / 2LL) * 2ULL + 1ULL;
    uint64_t mulZ = (uint64_t)(second / 2LL) * 2ULL + 1ULL;
    uint64_t mixed = ((uint64_t)(int64_t)chunkX * mulX
        + (uint64_t)(int64_t)chunkZ * mulZ) ^ (uint64_t)seed;
    random.setSeed((long long)mixed);
    return random;
}

} /* namespace */

extern "C" void gm_monument_candidate_for_region(
        long long worldSeed, int regionX, int regionZ,
        int *chunkX, int *chunkZ) {
    uint64_t mixed = (uint64_t)(int64_t)regionX * 341873128712ULL
        + (uint64_t)(int64_t)regionZ * 132897987541ULL
        + (uint64_t)worldSeed + 10387313ULL;
    Random random((long long)mixed);
    int x = regionX * 32
        + (random.nextInt(27) + random.nextInt(27)) / 2;
    int z = regionZ * 32
        + (random.nextInt(27) + random.nextInt(27)) / 2;
    if (chunkX) *chunkX = x;
    if (chunkZ) *chunkZ = z;
}

extern "C" int gm_monument_candidate(
        long long worldSeed, int chunkX, int chunkZ) {
    int adjustedX = chunkX < 0 ? chunkX - 31 : chunkX;
    int adjustedZ = chunkZ < 0 ? chunkZ - 31 : chunkZ;
    int candidateX, candidateZ;
    gm_monument_candidate_for_region(
        worldSeed, adjustedX / 32, adjustedZ / 32,
        &candidateX, &candidateZ);
    return chunkX == candidateX && chunkZ == candidateZ;
}

extern "C" int gm_monument_generate(
        long long worldSeed, int startChunkX, int startChunkZ,
        const GmMonumentAccess *access) {
    if (!access || !access->get_block || !access->set_block
            || access->sea_level < 1 || access->sea_level > 255)
        return 0;
    Random layout(worldSeed);
    long long mulX = layout.nextLong();
    long long mulZ = layout.nextLong();
    uint64_t mixed = (uint64_t)(int64_t)startChunkX * (uint64_t)mulX
        ^ (uint64_t)(int64_t)startChunkZ * (uint64_t)mulZ
        ^ (uint64_t)worldSeed;
    layout.setSeed((long long)mixed);
    static const int horizontal[4] = {
        F_NORTH, F_EAST, F_SOUTH, F_WEST
    };
    int facing = horizontal[layout.nextInt(4)];
    int originX = startChunkX * 16 + 8 - 29;
    int originZ = startChunkZ * 16 + 8 - 29;
    OceanMonumentPieces::MonumentBuilding building(
        &layout, originX, originZ, facing);
    Level level(access);
    BoundingBox *bounds = building.getBoundingBox();
    int minChunkX = floorDiv(bounds->x0, 16) - 2;
    int maxChunkX = floorDiv(bounds->x1, 16) + 2;
    int minChunkZ = floorDiv(bounds->z0, 16) - 2;
    int maxChunkZ = floorDiv(bounds->z1, 16) + 2;
    for (int chunkZ = minChunkZ; chunkZ <= maxChunkZ; ++chunkZ)
        for (int chunkX = minChunkX; chunkX <= maxChunkX; ++chunkX) {
            int minX = chunkX * 16 + 8;
            int minZ = chunkZ * 16 + 8;
            if (!bounds->intersects(minX, minZ, minX + 15, minZ + 15))
                continue;
            BoundingBox clip(minX, 1, minZ, minX + 15, 512, minZ + 15);
            Random placement = populationRandom(worldSeed, chunkX, chunkZ);
            building.postProcess(&level, &placement, &clip);
        }
    return facing;
}
