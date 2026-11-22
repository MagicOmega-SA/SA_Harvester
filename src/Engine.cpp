#include "Engine.h"

namespace
{
constexpr unsigned Map = 0x87F7E8u;
constexpr unsigned MapCells = 0x13Cu;
constexpr unsigned MapCellCount = 0x140u;
constexpr unsigned OutListCount = 0xA802C8u;
constexpr unsigned OutListCapacity = 128u;
constexpr unsigned DoListCount = 0x8B41F8u;
constexpr unsigned ClickedMission = 0x378u;
constexpr unsigned GetCellFloorHeight = 0x47B3A0u;
constexpr unsigned TargetFromObject = 0x6E6AB0u;

template<class T> T read(const void* object, unsigned offset)
{
    return *reinterpret_cast<const T*>(static_cast<const char*>(object) + offset);
}

bool gameFilePath(wchar_t (&path)[MAX_PATH], const wchar_t* file)
{
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return false;
    while (length && path[length - 1] != L'\\' && path[length - 1] != L'/') --length;
    if (length + lstrlenW(file) >= MAX_PATH) return false;
    lstrcpyW(path + length, file);
    return true;
}
}

SessionMode GameMode()
{
    return *reinterpret_cast<const volatile SessionMode*>(ADDR_SESSION);
}

bool IsNetworkMatch(SessionMode mode)
{
    return mode == SessionMode::Lan || mode == SessionMode::Internet;
}

bool PlausiblePointer(const void* pointer)
{
    const auto address = reinterpret_cast<ULONG_PTR>(pointer);
    return address >= 0x10000u && address < 0x80000000u && !(address & 3u);
}

bool Alive(void* object)
{
    return PlausiblePointer(object) && read<unsigned char>(object, OFF_ISALIVE) &&
        !read<unsigned char>(object, OFF_INLIMBO) && read<int>(object, OFF_HEALTH) > 0;
}

int VectorItems(unsigned address, void*** items)
{
    *items = nullptr;
    auto base = reinterpret_cast<const void*>(address);
    const int count = read<int>(base, OFF_VEC_COUNT);
    auto entries = read<void**>(base, OFF_VEC_ITEMS);
    if (count <= 0 || count > 20000 || !PlausiblePointer(entries)) return 0;
    *items = entries;
    return count;
}

void* MapCell(int x, int y)
{
    if (x < 0 || y < 0 || x >= 512 || y >= 512) return nullptr;
    auto map = reinterpret_cast<const void*>(Map);
    auto cells = read<void**>(map, MapCells);
    const int count = read<int>(map, MapCellCount);
    const int index = y * 512 + x;
    if (!PlausiblePointer(cells) || index >= count || count > 512 * 512) return nullptr;
    return cells[index];
}

bool CellFloor(int x, int y, int* height)
{
    using Floor = int (__attribute__((thiscall)) *)(void*, const POINT*);
    const POINT offset = {};
    void* cell = MapCell(x, y);
    if (!PlausiblePointer(cell)) return false;
    *height = reinterpret_cast<Floor>(GetCellFloorHeight)(cell, &offset);
    return *height >= -8192 && *height <= 8192;
}

int ObjectId(void* object)
{
    struct Target { int id; unsigned type; };
    using Encode = Target* (__attribute__((thiscall)) *)(Target*, void*);
    Target target = {};
    return object && reinterpret_cast<Encode>(TargetFromObject)(&target, object) ? target.id : -1;
}

bool SendOrder(void* unit, int mission, void* destination)
{
    if (!unit || !destination) return false;
    const SessionMode mode = GameMode();
    const bool network = IsNetworkMatch(mode);
    if (!network && mode != SessionMode::Campaign && mode != SessionMode::Skirmish) return false;
    const void* player = *reinterpret_cast<void* const*>(ADDR_CURPLAYER);
    if (!player || read<void*>(unit, OFF_OWNER) != player ||
        player == *reinterpret_cast<void* const*>(ADDR_OBSERVER)) return false;
    auto outCount = reinterpret_cast<const volatile unsigned*>(OutListCount);
    auto doCount = reinterpret_cast<const volatile unsigned*>(DoListCount);
    const unsigned beforeOut = *outCount;
    if (beforeOut >= OutListCapacity) return false;
    const unsigned beforeDo = *doCount;
    using NativeOrder = bool (__attribute__((thiscall)) *)(void*, int, void*, void*, void*);
    // Original ClickedMission: Enter and Harvest pass the refinery or cell as the destination.
    const auto send = read<NativeOrder>(read<void*>(unit, 0), ClickedMission);
    send(unit, mission, nullptr, destination, nullptr);
    // Existing single-player order extensions may dispatch straight to DoList.
    return *outCount > beforeOut || (!network && *doCount > beforeDo);
}

const wchar_t* EngineIniPath()
{
    static wchar_t path[MAX_PATH];
    return gameFilePath(path, L"SA_Harvester.ini") ? path : nullptr;
}

void LogEvent(const char* message)
{
#ifdef SA_HARVESTER_NO_LOG
    (void)message;
#else
    wchar_t path[MAX_PATH];
    if (!gameFilePath(path, L"SA_Harvester.log")) return;
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, message, lstrlenA(message), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
#endif
}
