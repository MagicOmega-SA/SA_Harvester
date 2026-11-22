#pragma once

#include <windows.h>

struct CellStruct { short X, Y; };

// Addresses for the Mental Omega 3.3.6 executable accepted by SyringeHandshake.
constexpr unsigned ADDR_BLD_ARR = 0xA8EB40u;
constexpr unsigned ADDR_CURPLAYER = 0xA83D4Cu;
constexpr unsigned ADDR_CUR_OBJECTS = 0x00A8ECB8u;
constexpr unsigned ADDR_FRAME = 0xA8ED84u;
constexpr unsigned ADDR_LOAD_PHASE = 0xA9FAB0u;
constexpr unsigned ADDR_OBSERVER = 0xAC1198u;
constexpr unsigned ADDR_PLANMODE = 0xAC4CF4u;
constexpr unsigned ADDR_SCENARIO = 0xA8B230u;
constexpr unsigned ADDR_SCEN_STARTED = 0xA8ED5Cu;
constexpr unsigned ADDR_SESSION = 0xA8B238u;
constexpr unsigned ADDR_SPECIAL_DIALOG = 0xA8EDA0u;
constexpr unsigned ADDR_UNIT_ARR = 0x8B4108u;
constexpr unsigned FN_CELL_GET_BUILDING = 0x0047C520u;
constexpr unsigned FN_CELL_GET_UNIT = 0x0047EBA0u;
constexpr unsigned FN_CELL_IS_SHROUDED = 0x00487950u;
constexpr unsigned FN_CELL_ORE_VALUE = 0x00485020u;
constexpr unsigned MISSION_ENTER = 7;
constexpr unsigned MISSION_MOVE = 2;
constexpr unsigned MISSION_UNLOAD = 16;
constexpr unsigned OFF_BLD_TYPE = 0x520u;
constexpr unsigned OFF_BLD_TYPE_QUEUE_CELL = 0x1618u;
constexpr unsigned OFF_FOOT_DESTINATION = 0x5A4u;
constexpr unsigned OFF_HEALTH = 0x6Cu;
constexpr unsigned OFF_INLIMBO = 0x81u;
constexpr unsigned OFF_ISALIVE = 0x90u;
constexpr unsigned OFF_HOUSE_HUMAN = 0x1ECu;
constexpr unsigned OFF_MISSION_CURRENT = 0xACu;
constexpr unsigned OFF_MISSION_QUEUED = 0xB4u;
constexpr unsigned OFF_MISSION_STATUS = 0xBCu;
constexpr unsigned OFF_OBJ_LOCATION = 0x009Cu;
constexpr unsigned OFF_OWNER = 0x21Cu;
constexpr unsigned OFF_RADIO_LINKS_ITEMS = 0xE4u;
constexpr unsigned OFF_RADIO_LINKS_SIZE = 0xE8u;
constexpr unsigned OFF_TECH_USELESS = 0x3D0u;
constexpr unsigned OFF_TYPE_DOCK_COUNT = 0x3F8u;
constexpr unsigned OFF_TYPE_DOCK_ITEMS = 0x3ECu;
constexpr unsigned OFF_TYPE_HARVESTER = 0xE0Eu;
constexpr unsigned OFF_TYPE_ID = 0x24u;
constexpr unsigned OFF_TYPE_STORAGE = 0x800u;
constexpr unsigned OFF_UNIT_TYPE = 0x6C4u;
constexpr unsigned OFF_VEC_COUNT = 16u;
constexpr unsigned OFF_VEC_ITEMS = 4u;
constexpr unsigned OFF_VT_DOCK_COORDS = 0xA8u;
constexpr unsigned OFF_VT_MOUSEOVER_CELL = 0x70u;
constexpr unsigned OFF_VT_MOUSEOVER_OBJECT = 0x74u;
constexpr unsigned OFF_VT_SAME_ZONE = 0x2CCu;
constexpr unsigned OFF_VT_STORAGE_PERCENT = 0x2B4u;

enum class SessionMode : int { Campaign = 0, Lan = 3, Internet = 4, Skirmish = 5 };

SessionMode GameMode();
bool IsNetworkMatch(SessionMode mode);
bool PlausiblePointer(const void* pointer);
bool Alive(void* object);
int VectorItems(unsigned arrayAddress, void*** items);
void* MapCell(int x, int y);
bool CellFloor(int x, int y, int* height);
int ObjectId(void* object);
bool SendOrder(void* unit, int mission, void* destination);
const wchar_t* EngineIniPath();
void LogEvent(const char* message);
