#include "Harvester.h"
#include "Engine.h"

namespace
{
constexpr int Harvest = 10;
constexpr int Guard = 5;
constexpr int Return = 12;
constexpr int Selling = 19;
constexpr int Construction = 18;
constexpr int MoveAction = 1;
constexpr int EnterAction = 3;
constexpr int HarvestAction = 6;
constexpr int MaxOrdersPerFrame = 4;
constexpr int PendingFrames = 90;
constexpr int ManualGraceFrames = 45;
constexpr int OreRadius = 8;
constexpr int MapSpan = 512;
constexpr int NoOreDistance = MapSpan * MapSpan * 2;
constexpr int OreSearchBudget = 2048;
constexpr unsigned UnitVtable = 0x7F5C70u;
constexpr unsigned BuildingVtable = 0x7E3EBCu;

struct WorldCoord { int x, y, z; };
struct OreSearch
{
    CellStruct origin, goal;
    int ring, distance;
    bool pending;
};
struct Miner
{
    void* unit;
    int id;
    int seen;
    WorldCoord position;
    double load;
    int mission, status;
    void* destination;
    int progressFrame, nextCheck, manualUntil;
    int attempts;
    bool fullPending, fromHarvest, protectedOrder;
    int pendingDockId, pendingUntil;
    CellStruct lastOre;
    bool knowsOre;
    CellStruct clearingGoal, clearingExit, clearingOrigin;
    int clearingUntil;
    OreSearch nearest;
};
struct Dock
{
    void* building;
    void* type;
    int id;
    WorldCoord position;
    void* cell;
    void* queueCell;
    void* occupant;
    void* waiting;
    void* claimant;
    bool unavailable, multipleClaims;
};

Miner miners[256];
Dock docks[128];
int minerCount, dockCount;
int lastFrame = -1;
void* lastScenario;
void* lastOwner;
bool fullEnabled, clearEnabled, nearestEnabled, multiplayerEnabled, initialized;
#ifdef SA_HARVESTER_NO_LOG
constexpr bool detailLog = false;
#else
bool detailLog;
#endif
int stallFrames;

template<class T> T read(const void* object, unsigned offset)
{
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}

template<class Fn> Fn method(const void* object, unsigned slot)
{
    return read<Fn>(read<void*>(object, 0), slot);
}

template<class T, unsigned N> constexpr int countOf(const T (&)[N]) { return N; }

void reset()
{
    minerCount = 0;
    dockCount = 0;
    lastFrame = -1;
    lastScenario = lastOwner = nullptr;
}

void loadSettings()
{
    const wchar_t* section = L"HarvesterAssist";
    const wchar_t* iniPath = EngineIniPath();
    fullEnabled = (iniPath ? GetPrivateProfileIntW(section, L"AutoFull", 0, iniPath) : 0) != 0;
    clearEnabled = (iniPath ? GetPrivateProfileIntW(section, L"AutoClear", 0, iniPath) : 0) != 0;
    nearestEnabled = iniPath && GetPrivateProfileIntW(section, L"AutoNearestOre", 1, iniPath) != 0;
    multiplayerEnabled =
        iniPath && GetPrivateProfileIntW(section, L"EnableMultiplayer", 0, iniPath) != 0;
#ifndef SA_HARVESTER_NO_LOG
    detailLog = iniPath && GetPrivateProfileIntW(section, L"Log", 0, iniPath) != 0;
#endif
    stallFrames = (iniPath ? GetPrivateProfileIntW(section, L"StallFrames", 150, iniPath) : 150);
    if (stallFrames < 60 || stallFrames > 900) stallFrames = 150;
    initialized = true;
}

bool isType(const void* type, const char* id)
{
    return lstrcmpiA(static_cast<const char*>(type) + OFF_TYPE_ID, id) == 0;
}

bool isRefinery(const void* type)
{
    return isType(type, "GAREFN") || isType(type, "NAREFN") ||
           isType(type, "CAREFN") || isType(type, "YARIREFN") ||
           isType(type, "FAREFN") || isType(type, "YAMREF") || isType(type, "GAOREP");
}

int cellX(const WorldCoord& pos) { return pos.x / 256; }
int cellY(const WorldCoord& pos) { return pos.y / 256; }

int cellDistance(const WorldCoord& from, const WorldCoord& to)
{
    const int dx = cellX(from) - cellX(to);
    const int dy = cellY(from) - cellY(to);
    return dx * dx + dy * dy;
}

bool sameZone(void* unit, const WorldCoord& pos)
{
    using Fn = bool (__attribute__((thiscall)) *)(void*, const WorldCoord*);
    return method<Fn>(unit, OFF_VT_SAME_ZONE)(unit, &pos);
}

bool collectDocks(void* owner)
{
    using DockCoords = WorldCoord* (__attribute__((thiscall)) *)(void*, WorldCoord*, void*);
    using CellUnit = void* (__attribute__((thiscall)) *)(void*, bool);
    void** buildings = nullptr;
    const int count = VectorItems(ADDR_BLD_ARR, &buildings);
    dockCount = 0;
    for (int i = 0; i < count; ++i)
    {
        void* building = buildings[i];
        if (!Alive(building) || read<unsigned>(building, 0) != BuildingVtable ||
            read<void*>(building, OFF_OWNER) != owner) continue;
        const int mission = read<int>(building, OFF_MISSION_CURRENT);
        if (mission == Selling || mission == Construction) continue;
        void* type = read<void*>(building, OFF_BLD_TYPE);
        if (!PlausiblePointer(type) || !isRefinery(type)) continue;
        if (dockCount == countOf(docks)) return false;

        WorldCoord position = {};
        method<DockCoords>(building, OFF_VT_DOCK_COORDS)(building, &position, nullptr);
        void* cell = MapCell(cellX(position), cellY(position));
        const int id = ObjectId(building);
        if (!cell || id < 0) continue;
        const auto unitAt = reinterpret_cast<CellUnit>(FN_CELL_GET_UNIT);
        void* occupant = unitAt(cell, false);
        const int queueX = read<int>(type, OFF_BLD_TYPE_QUEUE_CELL);
        const int queueY = read<int>(type, OFF_BLD_TYPE_QUEUE_CELL + 4);
        const auto origin = read<WorldCoord>(building, OFF_OBJ_LOCATION);
        void* queueCell = (queueX || queueY) && queueX >= -16 && queueX <= 16 &&
            queueY >= -16 && queueY <= 16
            ? MapCell(cellX(origin) + queueX, cellY(origin) + queueY) : nullptr;
        void* waiting = queueCell && queueCell != cell ? unitAt(queueCell, false) : nullptr;
        const int size = read<int>(building, OFF_RADIO_LINKS_SIZE);
        auto items = read<void**>(building, OFF_RADIO_LINKS_ITEMS);
        const bool unknownLinks = size != 1 || !PlausiblePointer(items);
        docks[dockCount++] = {building, type, id, position, cell, queueCell, occupant, waiting,
                              unknownLinks ? nullptr : *items, unknownLinks, false};
    }
    return true;
}

bool mineStage(const Miner& miner)
{
    // Mission_Harvest states 0/1 find and collect ore. States 2/3 head to the refinery.
    return miner.mission == Harvest && (miner.status == 0 || miner.status == 1);
}

bool refineryMission(int mission)
{
    return mission == MISSION_ENTER || mission == MISSION_UNLOAD || mission == Return;
}

bool returnStage(const Miner& miner)
{
    return refineryMission(miner.mission) ||
           (miner.mission == Harvest && (miner.status == 2 || miner.status == 3));
}

bool selected(void* object, void** selection, int count)
{
    for (int i = 0; i < count; ++i)
        if (selection[i] == object) return true;
    return false;
}

bool progresses(const Miner& miner, const WorldCoord& pos, double load)
{
    return miner.position.x != pos.x || miner.position.y != pos.y ||
           miner.load != load;
}

Miner* observe(void* unit, int frame, bool userSelected)
{
    using StoragePercent = double (__attribute__((thiscall)) *)(void*);
    const int id = ObjectId(unit);
    if (id < 0) return nullptr;
    const double load = method<StoragePercent>(unit, OFF_VT_STORAGE_PERCENT)(unit);
    if (!(load >= 0.0 && load <= 1.0)) return nullptr;
    const auto pos = read<WorldCoord>(unit, OFF_OBJ_LOCATION);
    const int mission = read<int>(unit, OFF_MISSION_CURRENT);
    const int status = read<int>(unit, OFF_MISSION_STATUS);
    void* destination = read<void*>(unit, OFF_FOOT_DESTINATION);
    const int queued = read<int>(unit, OFF_MISSION_QUEUED);
    const bool manual = userSelected || queued != -1;

    int index = 0;
    while (index < minerCount && (miners[index].unit != unit || miners[index].id != id))
        ++index;
    if (index == minerCount)
    {
        if (minerCount == countOf(miners)) return nullptr;
        ++minerCount;
        miners[index] = {unit, id, frame, pos, load, mission, status, destination,
                         frame, 0, manual ? frame + ManualGraceFrames : 0,
                         0, false, mission == Harvest, manual, -1, 0, {}, false, {}, {}, {}, 0, {}};
        return &miners[index];
    }

    Miner& miner = miners[index];
    if (manual) miner.manualUntil = frame + ManualGraceFrames;
    const bool protectedOrder = frame < miner.manualUntil;
    if (progresses(miner, pos, load) || protectedOrder)
        miner.progressFrame = frame;
    if (load != miner.load || manual) miner.attempts = 0;
    if (mission == Harvest && status == 1 && load > miner.load)
    {
        miner.lastOre = {static_cast<short>(cellX(pos)), static_cast<short>(cellY(pos))};
        miner.knowsOre = true;
    }
    if (load < 1.0 || protectedOrder) miner.fullPending = false;
    if (load == 1.0 && miner.load < 1.0 && !protectedOrder) miner.fullPending = true;
    const bool otherOrder = queued != -1 && queued != Harvest && !refineryMission(queued) &&
        !(queued == MISSION_MOVE && miner.clearingUntil);
    if (userSelected || otherOrder || load > 0.0) miner.nearest.pending = false;
    else if (nearestEnabled && miner.load > 0.0 && load == 0.0 &&
             (returnStage(miner) || refineryMission(mission)))
        miner.nearest = {{}, {}, -1, NoOreDistance, true};
    if (frame >= miner.pendingUntil || load < miner.load) miner.pendingDockId = -1;
    if (userSelected || load > 0.0 || frame >= miner.clearingUntil)
        miner.clearingUntil = 0;
    if (mission == Harvest) miner.fromHarvest = true;
    else if (mission != Guard && mission != MISSION_ENTER &&
             mission != MISSION_UNLOAD && mission != Return) miner.fromHarvest = false;

    miner.seen = frame;
    miner.position = pos;
    miner.load = load;
    miner.mission = mission;
    miner.status = status;
    miner.destination = destination;
    miner.protectedOrder = protectedOrder;
    if (!mineStage(miner)) miner.fullPending = false;
    return &miner;
}

bool compatible(const Miner& miner, const Dock& dock)
{
    void* type = read<void*>(miner.unit, OFF_UNIT_TYPE);
    auto types = read<void**>(type, OFF_TYPE_DOCK_ITEMS);
    const int count = read<int>(type, OFF_TYPE_DOCK_COUNT);
    if (count < 1 || count > 32 || !PlausiblePointer(types)) return false;
    for (int i = 0; i < count; ++i)
        if (types[i] == dock.type) return true;
    return false;
}

bool canEnter(void* unit, const Dock& dock)
{
    using MouseOverObject = int (__attribute__((thiscall)) *)(void*, void*, bool);
    return method<MouseOverObject>(unit, OFF_VT_MOUSEOVER_OBJECT)(unit, dock.building, true)
        == EnterAction;
}

void claim(Dock& dock, void* unit)
{
    if (!dock.claimant) dock.claimant = unit;
    else if (dock.claimant != unit) dock.multipleClaims = true;
}

Dock* freeDock(const Miner& miner, bool recovery)
{
    Dock* best = nullptr;
    Dock* own = nullptr;
    int bestDistance = 0;
    for (int i = 0; i < dockCount; ++i)
    {
        Dock& dock = docks[i];
        if (dock.unavailable || dock.occupant || dock.multipleClaims ||
            (dock.waiting && (!recovery || dock.waiting != miner.unit)) ||
            !compatible(miner, dock)) continue;
        if (dock.claimant)
        {
            if (recovery && dock.claimant == miner.unit) own = &dock;
            continue;
        }
        const int distance = cellDistance(miner.position, dock.position);
        if (best && (distance > bestDistance ||
                     (distance == bestDistance && dock.id >= best->id))) continue;
        if (!sameZone(miner.unit, dock.position) || !canEnter(miner.unit, dock)) continue;
        best = &dock;
        bestDistance = distance;
    }
    if (best) return best;
    return own && sameZone(miner.unit, own->position) && canEnter(miner.unit, *own)
        ? own : nullptr;
}

bool cellAction(void* unit, int x, int y, int expected)
{
    using MouseOverCell = int (__attribute__((thiscall)) *)(void*, const CellStruct*, bool, bool);
    WorldCoord pos = {x * 256 + 128, y * 256 + 128, 0};
    if (!CellFloor(x, y, &pos.z) || !sameZone(unit, pos)) return false;
    const CellStruct grid = {static_cast<short>(x), static_cast<short>(y)};
    return method<MouseOverCell>(unit, OFF_VT_MOUSEOVER_CELL)(unit, &grid, true, true)
        == expected;
}

bool canHarvest(void* unit, int x, int y)
{
    using OreValue = int (__attribute__((thiscall)) *)(void*);
    using IsShrouded = bool (__attribute__((thiscall)) *)(void*);
    void* cell = MapCell(x, y);
    if (!cell || (IsNetworkMatch(GameMode()) &&
        reinterpret_cast<IsShrouded>(FN_CELL_IS_SHROUDED)(cell))) return false;
    return reinterpret_cast<OreValue>(FN_CELL_ORE_VALUE)(cell) > 0 &&
        cellAction(unit, x, y, HarvestAction);
}

bool findOreNear(void* unit, int x, int y, const CellStruct* excluded, CellStruct& goal)
{
    int bestDistance = OreRadius * OreRadius + 1;
    for (int dy = -OreRadius; dy <= OreRadius; ++dy)
    {
        for (int dx = -OreRadius; dx <= OreRadius; ++dx)
        {
            if (excluded && x + dx == excluded->X && y + dy == excluded->Y) continue;
            const int distance = dx * dx + dy * dy;
            if (distance >= bestDistance || !canHarvest(unit, x + dx, y + dy)) continue;
            bestDistance = distance;
            goal = {static_cast<short>(x + dx), static_cast<short>(y + dy)};
        }
    }
    return bestDistance <= OreRadius * OreRadius;
}

bool order(const Miner& miner, int mission, void* destination)
{
    return destination && ObjectId(miner.unit) == miner.id &&
        SendOrder(miner.unit, mission, destination);
}

bool dispatchFull(Miner& miner, int frame, bool recovery)
{
    Dock* dock = freeDock(miner, recovery);
    if (!dock) return false;
    if (!order(miner, MISSION_ENTER, dock->building)) return false;
    claim(*dock, miner.unit);
    miner.fullPending = false;
    miner.pendingDockId = dock->id;
    miner.pendingUntil = frame + PendingFrames;
    if (detailLog) LogEvent(recovery ? "harvester: recover unload" : "harvester: full unload");
    return true;
}

bool orderOnCell(const Miner& miner, int mission, const CellStruct& cell)
{
    return order(miner, mission, MapCell(cell.X, cell.Y));
}

bool isDockCell(void* cell)
{
    for (int i = 0; i < dockCount; ++i)
        if (docks[i].cell == cell || docks[i].queueCell == cell) return true;
    return false;
}

bool findExit(const Miner& miner, const CellStruct& ore, CellStruct& exit)
{
    using CellUnit = void* (__attribute__((thiscall)) *)(void*, bool);
    using CellBuilding = void* (__attribute__((thiscall)) *)(void*);
    const auto unitAt = reinterpret_cast<CellUnit>(FN_CELL_GET_UNIT);
    const auto buildingAt = reinterpret_cast<CellBuilding>(FN_CELL_GET_BUILDING);
    constexpr int directions[][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1},
                                     {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    int bestDistance = 512 * 512 * 2;
    bool found = false;
    for (const auto& direction : directions)
    {
        const int x = cellX(miner.position) + direction[0];
        const int y = cellY(miner.position) + direction[1];
        const int dx = x - ore.X, dy = y - ore.Y;
        const int distance = dx * dx + dy * dy;
        if (distance >= bestDistance) continue;
        void* cell = MapCell(x, y);
        if (!cell || isDockCell(cell) || unitAt(cell, false) || buildingAt(cell) ||
            !cellAction(miner.unit, x, y, MoveAction)) continue;
        exit = {static_cast<short>(x), static_cast<short>(y)};
        bestDistance = distance;
        found = true;
    }
    return found;
}

bool restartHarvest(Miner& miner, int frame, bool clearingDock)
{
    CellStruct cell = {};
    const CellStruct current = {static_cast<short>(cellX(miner.position)),
                                static_cast<short>(cellY(miner.position))};
    const CellStruct* excluded = clearingDock ? &current : nullptr;
    const bool previousField = miner.knowsOre &&
        findOreNear(miner.unit, miner.lastOre.X, miner.lastOre.Y, excluded, cell);
    if (!previousField && !findOreNear(miner.unit, current.X, current.Y, excluded, cell))
        return false;
    // Restarting an already-current Harvest mission can leave its old state in place.
    // An empty blocker leaves on a Move order, then resumes harvesting once out of the way.
    if (clearingDock)
    {
        CellStruct exit = {};
        if (!findExit(miner, cell, exit) || !orderOnCell(miner, MISSION_MOVE, exit))
            return false;
        miner.clearingGoal = cell;
        miner.clearingExit = exit;
        miner.clearingOrigin = current;
        miner.clearingUntil = frame + stallFrames;
    }
    else if (!orderOnCell(miner, Harvest, cell)) return false;
    if (detailLog) LogEvent(clearingDock ? "harvester: clear dock move" : "harvester: recover harvest");
    return true;
}

bool occupiesDock(const Miner& miner, bool includeQueue)
{
    for (int i = 0; i < dockCount; ++i)
        if (docks[i].occupant == miner.unit ||
            (includeQueue && docks[i].waiting == miner.unit)) return true;
    return false;
}

bool readyForNearest(const Miner& miner)
{
    if (miner.pendingDockId >= 0 || returnStage(miner) ||
        (miner.mission != Harvest && miner.mission != Guard) || occupiesDock(miner, true))
        return false;
    for (int i = 0; i < dockCount; ++i)
        if (docks[i].claimant == miner.unit) return false;
    return true;
}

void considerOre(const Miner& miner, OreSearch& search, int dx, int dy)
{
    const int distance = dx * dx + dy * dy;
    if (distance >= search.distance) return;
    const int x = search.origin.X + dx, y = search.origin.Y + dy;
    if (!canHarvest(miner.unit, x, y)) return;
    search.distance = distance;
    search.goal = {static_cast<short>(x), static_cast<short>(y)};
}

bool retargetNearest(Miner& miner, int& budget)
{
    OreSearch& search = miner.nearest;
    if (search.ring < 0)
    {
        search.origin = {static_cast<short>(cellX(miner.position)),
                         static_cast<short>(cellY(miner.position))};
        search.ring = 0;
    }

    // The next ring's radius is a lower bound on every unvisited cell's distance.
    int allowance = OreSearchBudget / 4;
    while (budget > 0 && allowance > 0 && search.ring < MapSpan &&
           search.ring * search.ring < search.distance)
    {
        const int r = search.ring++;
        if (r == 0) considerOre(miner, search, 0, 0);
        else
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                considerOre(miner, search, dx, -r);
                considerOre(miner, search, dx, r);
            }
            for (int dy = 1 - r; dy < r; ++dy)
            {
                considerOre(miner, search, -r, dy);
                considerOre(miner, search, r, dy);
            }
        }
        const int examined = r ? 8 * r : 1;
        budget -= examined;
        allowance -= examined;
    }
    if (search.ring < MapSpan && search.ring * search.ring < search.distance) return false;
    if (search.distance == NoOreDistance)
    {
        search.pending = false;
        return false;
    }
    const CellStruct goal = search.goal;
    if (!canHarvest(miner.unit, goal.X, goal.Y))
    {
        search = {{}, {}, -1, NoOreDistance, true};
        return false;
    }
    void* destination = MapCell(goal.X, goal.Y);
    if (mineStage(miner) && (miner.destination == destination ||
        (miner.status == 1 && cellX(miner.position) == goal.X && cellY(miner.position) == goal.Y)))
    {
        search.pending = false;
        return false;
    }
    if (!order(miner, Harvest, destination)) return false;
    search.pending = false;
    miner.clearingUntil = 0;
    if (detailLog)
    {
        char message[96];
        wsprintfA(message, "harvester: nearest ore from (%d,%d) to (%d,%d)",
                  search.origin.X, search.origin.Y, goal.X, goal.Y);
        LogEvent(message);
    }
    return true;
}

bool resumeAfterClearing(Miner& miner, int frame)
{
    if (frame < miner.nextCheck) return false;
    const int x = cellX(miner.position), y = cellY(miner.position);
    const bool departed = x != miner.clearingOrigin.X || y != miner.clearingOrigin.Y;
    const CellStruct& exit = miner.clearingExit;
    const bool movingToExit = miner.mission == MISSION_MOVE && miner.destination &&
        miner.destination == MapCell(exit.X, exit.Y);
    if (!departed || occupiesDock(miner, true) || (!movingToExit && miner.mission != Guard))
        return false;
    CellStruct goal = miner.clearingGoal;
    const bool hasOre = canHarvest(miner.unit, goal.X, goal.Y) ||
        findOreNear(miner.unit, x, y, nullptr, goal);
    if (!hasOre || !orderOnCell(miner, Harvest, goal))
    {
        miner.nextCheck = frame + 15;
        return false;
    }
    miner.clearingUntil = 0;
    if (detailLog) LogEvent("harvester: resume after clearing dock");
    return true;
}

void markApproaches(const Miner& miner)
{
    for (int i = 0; i < dockCount; ++i)
    {
        Dock& dock = docks[i];
        if (miner.pendingDockId == dock.id ||
            (returnStage(miner) && (miner.destination == dock.building ||
                                    miner.destination == dock.cell ||
                                    (dock.queueCell && miner.destination == dock.queueCell))))
            claim(dock, miner.unit);
    }
}
} // namespace

void HarvesterPoll()
{
    if (!initialized) loadSettings();
    if (!fullEnabled && !clearEnabled && !nearestEnabled) return;
    const SessionMode mode = GameMode();
    const bool network = IsNetworkMatch(mode);
    const bool supported = mode == SessionMode::Campaign || mode == SessionMode::Skirmish ||
        (network && multiplayerEnabled);
    void* owner = *reinterpret_cast<void**>(ADDR_CURPLAYER);
    void* scenario = *reinterpret_cast<void**>(ADDR_SCENARIO);
    if (!supported || !PlausiblePointer(owner) || !scenario ||
        owner == *reinterpret_cast<void* const*>(ADDR_OBSERVER) ||
        (network && !read<unsigned char>(owner, OFF_HOUSE_HUMAN)) ||
        *reinterpret_cast<const int*>(ADDR_LOAD_PHASE) ||
        !*reinterpret_cast<const unsigned char*>(ADDR_SCEN_STARTED))
    {
        reset();
        return;
    }
    const int frame = *reinterpret_cast<const int*>(ADDR_FRAME);
    if (scenario != lastScenario || owner != lastOwner || frame < lastFrame) reset();
    if (frame == lastFrame || *reinterpret_cast<const int*>(ADDR_SPECIAL_DIALOG) ||
        *reinterpret_cast<const unsigned char*>(ADDR_PLANMODE)) return;
    lastScenario = scenario;
    lastOwner = owner;
    lastFrame = frame;

    void** units = nullptr;
    const int unitCount = VectorItems(ADDR_UNIT_ARR, &units);
    void** selection = nullptr;
    const int selectionCount = VectorItems(ADDR_CUR_OBJECTS, &selection);
    bool allTracked = true;
    for (int i = 0; i < unitCount; ++i)
    {
        void* unit = units[i];
        if (!Alive(unit) || read<unsigned>(unit, 0) != UnitVtable ||
            read<void*>(unit, OFF_OWNER) != owner) continue;
        void* type = read<void*>(unit, OFF_UNIT_TYPE);
        if (!PlausiblePointer(type) || !read<unsigned char>(type, OFF_TYPE_HARVESTER) ||
            read<int>(type, OFF_TYPE_STORAGE) <= 0) continue;
        if (!observe(unit, frame, selected(unit, selection, selectionCount))) allTracked = false;
    }
    for (int i = 0; i < minerCount;)
    {
        if (miners[i].seen == frame) ++i;
        else miners[i] = miners[--minerCount];
    }
    if (!allTracked || !minerCount || !collectDocks(owner)) return;
    for (int i = 0; i < minerCount; ++i) markApproaches(miners[i]);

    int orders = 0;
    int oreBudget = OreSearchBudget;
    for (int i = 0; i < minerCount && orders < MaxOrdersPerFrame; ++i)
    {
        Miner& miner = miners[i];
        if (miner.nearest.pending && readyForNearest(miner))
        {
            if (retargetNearest(miner, oreBudget)) { ++orders; continue; }
            if (miner.nearest.pending) continue;
        }
        if (!dockCount || miner.protectedOrder || miner.pendingDockId >= 0) continue;
        if (miner.clearingUntil)
        {
            if (!miner.nearest.pending && resumeAfterClearing(miner, frame)) ++orders;
            continue;
        }
        const bool stalled = clearEnabled && miner.attempts < 2 && frame >= miner.nextCheck &&
            frame - miner.progressFrame >= stallFrames;
        const bool lostOre = stalled && miner.mission == Guard && miner.fromHarvest &&
            read<unsigned char>(miner.unit, OFF_TECH_USELESS);
        bool sent = false;
        if (miner.load == 1.0)
        {
            if (fullEnabled && miner.fullPending && mineStage(miner))
                sent = dispatchFull(miner, frame, false);
            if (!sent && stalled && (miner.mission == Harvest || returnStage(miner) || lostOre) &&
                !occupiesDock(miner, false))
                sent = dispatchFull(miner, frame, true);
        }
        else if (stalled)
        {
            const bool waitingForOre = miner.mission == Harvest && miner.status == 4;
            const bool emptyBlocker = miner.load == 0.0 && occupiesDock(miner, true) &&
                (returnStage(miner) || miner.mission == Harvest);
            if (mineStage(miner) || waitingForOre || lostOre || emptyBlocker)
                sent = restartHarvest(miner, frame, emptyBlocker);
        }
        if (sent)
        {
            ++orders;
            ++miner.attempts;
            miner.progressFrame = frame;
        }
        else if (stalled)
        {
            // Revisit a blocked dock or a newly regenerated ore field without scanning every frame.
            miner.nextCheck = frame + 15;
        }
    }
}
