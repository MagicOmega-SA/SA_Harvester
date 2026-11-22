#include <windows.h>
#include "Harvester.h"

namespace
{
struct Hook
{
    unsigned address;
    unsigned size;
    const char* name;
    unsigned reserved;
};

const Hook hooks[] __attribute__((section(".syhks00"), used, aligned(16))) =
{
    {0x55D3BBu, 5, "SA_Harvester_Update", 0}, // Active game, before native event processing.
};

struct HandshakeInfo
{
    int cbSize;
    int numHooks;
    unsigned checksum;
    DWORD exeFilesize;
    DWORD exeTimestamp;
    unsigned exeCRC;
    int messageCapacity;
    char* message;
};

static_assert(sizeof(Hook) == 16 && sizeof(HandshakeInfo) == 32);
}

extern "C" __declspec(dllexport) HRESULT __cdecl SyringeHandshake(HandshakeInfo* info)
{
    if (!info || info->cbSize < static_cast<int>(sizeof(HandshakeInfo))) return E_FAIL;
    const bool supported = info->exeFilesize == 4816864 && info->exeTimestamp == 0x3BDF544E &&
        info->exeCRC == 0x1B499086;
    if (info->message && info->messageCapacity > 0)
        lstrcpynA(info->message, supported ? "SA_Harvester 0.3.0" :
            "SA_Harvester requires the Mental Omega 3.3.6 game executable", info->messageCapacity);
    return supported ? S_OK : S_FALSE;
}

extern "C" __declspec(dllexport) DWORD __cdecl SA_Harvester_Update(void*)
{
    HarvesterPoll();
    return 0;
}

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID)
{
    return TRUE;
}
