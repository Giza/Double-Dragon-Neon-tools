#include <windows.h>
#include <cstdint>
#include <string>
#include <Psapi.h>

// Смещение функции относительно базы модуля
const uintptr_t FUNCTION_OFFSET = 0x9C9D0; // 0xF5C9D0 - 0xEC0000

// Адрес оригинальной функции (будет вычислен позже)
uintptr_t originalFunctionAddress = 0;

// Буфер для хранения оригинальных байтов
unsigned char originalBytes[256];

// Путь к системному d3d9.dll
char systemD3D9Path[MAX_PATH];

// Хэндл оригинального d3d9.dll
HMODULE hOriginalDll = NULL;

// Адреса системных функций
typedef void* (__cdecl* MemsetFunc)(void* dest, int val, size_t size);
MemsetFunc memsetAddr = NULL;

// Адреса функций из игры
uintptr_t sub_513880 = 0;
uintptr_t sub_5cf850 = 0;
uintptr_t sub_651d93 = 0;

// D3D9 функции
typedef void* (__stdcall* Direct3DCreate9Func)(UINT SDKVersion);
typedef HRESULT(__stdcall* Direct3DCreate9ExFunc)(UINT SDKVersion, void** ppD3D);

Direct3DCreate9Func originalDirect3DCreate9 = NULL;
Direct3DCreate9ExFunc originalDirect3DCreate9Ex = NULL;

// Создаем лог-файл для отладки
void WriteLog(const char* message)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string logPath = std::string(path);
    logPath = logPath.substr(0, logPath.find_last_of("\\/")) + "\\hook_log.txt";

    FILE* file;
    fopen_s(&file, logPath.c_str(), "a");
    if (file)
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(file, "[%02d:%02d:%02d] %s\n", st.wHour, st.wMinute, st.wSecond, message);
        fclose(file);
    }
}

// Получение адреса функции из call
uintptr_t GetCallTarget(uintptr_t callInstructionAddress)
{
    // Читаем относительный адрес из инструкции call
    DWORD relativeAddress;
    memcpy(&relativeAddress, (void*)(callInstructionAddress + 1), 4);

    // Вычисляем абсолютный адрес (callInstructionAddress + 5 + relativeAddress)
    return callInstructionAddress + 5 + relativeAddress;
}

// Получение базового адреса модуля
uintptr_t GetModuleBaseAddress()
{
    HMODULE hModule = GetModuleHandleA(NULL);
    if (!hModule) return 0;

    MODULEINFO moduleInfo;
    if (GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo)))
    {
        char message[256];
        sprintf_s(message, "Module base address: 0x%p, Size: 0x%X", moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage);
        WriteLog(message);
        return (uintptr_t)moduleInfo.lpBaseOfDll;
    }
    return 0;
}

// Инициализация адресов функций
bool InitializeFunctions()
{
    char message[256];

    // Получаем базовый адрес модуля
    uintptr_t baseAddress = GetModuleBaseAddress();
    if (!baseAddress)
    {
        WriteLog("Failed to get base address");
        return false;
    }

    // Вычисляем реальный адрес функции
    originalFunctionAddress = baseAddress + FUNCTION_OFFSET;

    sprintf_s(message, "Base address: 0x%p, Function offset: 0x%X, Final address: 0x%p",
        (void*)baseAddress, FUNCTION_OFFSET, (void*)originalFunctionAddress);
    WriteLog(message);

    HMODULE hModule = GetModuleHandleA(NULL);
    if (!hModule)
    {
        WriteLog("Failed to get game module handle");
        return false;
    }

    // Получаем адрес memset из msvcrt.dll
    HMODULE hMsvcrt = LoadLibraryA("msvcrt.dll");
    if (!hMsvcrt)
    {
        WriteLog("Failed to load msvcrt.dll");
        return false;
    }

    memsetAddr = (MemsetFunc)GetProcAddress(hMsvcrt, "memset");
    if (!memsetAddr)
    {
        WriteLog("Failed to get memset address");
        return false;
    }

    // Проверяем первые байты функции
    unsigned char firstBytes[5];
    memcpy(firstBytes, (void*)originalFunctionAddress, 5);
    sprintf_s(message, "First bytes at target address: %02X %02X %02X %02X %02X",
        firstBytes[0], firstBytes[1], firstBytes[2], firstBytes[3], firstBytes[4]);
    WriteLog(message);

    // Сохраняем оригинальные байты функции
    memcpy(originalBytes, (void*)originalFunctionAddress, sizeof(originalBytes));

    // Ищем адреса call в оригинальной функции
    uintptr_t currentAddress = originalFunctionAddress;
    int foundCalls = 0;

    for (int i = 0; i < sizeof(originalBytes) - 5; i++)
    {
        if (originalBytes[i] == 0xE8) // Инструкция CALL
        {
            uintptr_t callAddress = currentAddress + i;
            uintptr_t targetAddress = GetCallTarget(callAddress);

            // Определяем, какая это функция по порядку
            if (foundCalls == 0) // Первый call - memset
            {
                // memset мы уже получили из msvcrt.dll
            }
            else if (foundCalls == 1) // Второй call - sub_513880
            {
                sub_513880 = targetAddress;
            }
            else if (foundCalls == 3) // Третий call - sub_5cf850
            {
                sub_5cf850 = targetAddress;
            }
            else if (foundCalls == 4) // Четвертый call - sub_651d93
            {
                sub_651d93 = targetAddress;
            }

            foundCalls++;
        }
    }

    sprintf_s(message, "Functions initialized: memset=0x%p, sub_513880=0x%p, sub_5cf850=0x%p, sub_651d93=0x%p",
        memsetAddr, (void*)sub_513880, (void*)sub_5cf850, (void*)sub_651d93);
    WriteLog(message);

    return foundCalls >= 4; // Должны найти как минимум 4 call
}

// Наша новая функция
__declspec(naked) void hookFunction()
{
    __asm
    {
        push    ebp
        mov     ebp, esp
        sub     esp, 0x127        // Увеличенный размер стека (было 0x84) 127
        mov     eax, dword ptr fs : [0x14]    // Правильный способ получить security cookie
        xor eax, ebp
        mov     dword ptr[ebp - 0x4], eax
        push    ebx
        push    0x122            // Увеличенное значение (было 0x7f) yf 122
        lea     eax, [ebp - 0x126] //83
        push    0x0
        push    eax
        mov     byte ptr[ebp - 0x127], 0x0 //84

        // Вызов memset через сохраненный адрес
        call    memsetAddr

        add     esp, 0xc
        cmp     dword ptr[esi + 0xc0], 0x3
        lea     ebx, [esi + 0x3764]
        jne     hook_part2
        lea     ecx, [ebp - 0x127] //84
        push    ecx
        mov     ecx, dword ptr[esi + 0x376c]

        // Вызов sub_513880 через сохраненный адрес
        call    sub_513880

        add     esp, 0x4
        lea     edx, [ebp - 0x127] //84
        push    edx
        jmp     hook_end
        hook_part2 :
        mov     ecx, dword ptr[esi + 0x376c]
            lea     eax, [ebp - 0x127] //84
            add     ecx, 0xa
            push    eax

            // Вызов sub_513880 через сохраненный адрес
            call    sub_513880

            add     esp, 0x4
            lea     ecx, [ebp - 0x127] //
            push    ecx
            hook_end :
        lea     eax, [esi + 0x3224]

            // Вызов sub_5cf850 через сохраненный адрес
            call    sub_5cf850

            mov     ecx, dword ptr[ebp - 0x4]
            xor ecx, ebp
            pop     ebx

            // Вызов sub_651d93 через сохраненный адрес
            //call    sub_651d93

            mov     esp, ebp
            pop     ebp
            ret
    }
}

// Загрузка оригинального d3d9.dll
bool LoadOriginalDll()
{
    GetSystemDirectoryA(systemD3D9Path, MAX_PATH);
    strcat_s(systemD3D9Path, "\\d3d9.dll");

    hOriginalDll = LoadLibraryA(systemD3D9Path);
    if (!hOriginalDll)
    {
        //WriteLog("Failed to load original d3d9.dll");
        return false;
    }

    originalDirect3DCreate9 = (Direct3DCreate9Func)GetProcAddress(hOriginalDll, "Direct3DCreate9");
    originalDirect3DCreate9Ex = (Direct3DCreate9ExFunc)GetProcAddress(hOriginalDll, "Direct3DCreate9Ex");

    if (!originalDirect3DCreate9 || !originalDirect3DCreate9Ex)
    {
        //WriteLog("Failed to get d3d9.dll function addresses");
        return false;
    }

    return true;
}

// Функция для установки хука
bool SetHook()
{
    WriteLog("Attempting to set hook...");

    DWORD oldProtect;
    char message[256];

    // Сохраняем оригинальные байты
    memcpy(originalBytes, (void*)originalFunctionAddress, sizeof(originalBytes));
    sprintf_s(message, "Original bytes saved from address: 0x%X", originalFunctionAddress);
    WriteLog(message);

    // Изменяем защиту памяти
    if (!VirtualProtect((LPVOID)originalFunctionAddress, sizeof(originalBytes), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteLog("Failed to change memory protection!");
        MessageBoxA(NULL, "Failed to change memory protection!", "Hook Error", MB_ICONERROR);
        return false;
    }

    // Создаем jump на нашу функцию
    *(unsigned char*)originalFunctionAddress = 0xE9;  // JMP
    *(DWORD*)(originalFunctionAddress + 1) = (DWORD)hookFunction - originalFunctionAddress - 5;

    sprintf_s(message, "Hook installed. Jump to address: 0x%X", (DWORD)hookFunction);
    WriteLog(message);

    // Проверяем установленные байты
    unsigned char installedBytes[5];
    memcpy(installedBytes, (void*)originalFunctionAddress, 5);
    sprintf_s(message, "Installed bytes: %02X %02X %02X %02X %02X",
        installedBytes[0], installedBytes[1], installedBytes[2], installedBytes[3], installedBytes[4]);
    WriteLog(message);

    // Восстанавливаем защиту памяти
    VirtualProtect((LPVOID)originalFunctionAddress, sizeof(originalBytes), oldProtect, &oldProtect);

    return true;
}

// Экспортируемые функции
extern "C" {
    __declspec(dllexport) void* __stdcall Direct3DCreate9(UINT SDKVersion)
    {
        if (originalDirect3DCreate9)
            return originalDirect3DCreate9(SDKVersion);
        return NULL;
    }

    __declspec(dllexport) HRESULT __stdcall Direct3DCreate9Ex(UINT SDKVersion, void** ppD3D)
    {
        if (originalDirect3DCreate9Ex)
            return originalDirect3DCreate9Ex(SDKVersion, ppD3D);
        return E_FAIL;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        //WriteLog("DLL attached to process");
        if (!LoadOriginalDll())
        {
            MessageBoxA(NULL, "Failed to load original d3d9.dll!", "Error", MB_ICONERROR);
            return FALSE;
        }
        if (!InitializeFunctions())
        {
            MessageBoxA(NULL, "Failed to initialize functions!", "Error", MB_ICONERROR);
            return FALSE;
        }
        //MessageBoxA(NULL, "DLL successfully loaded!", "Hook Status", MB_ICONINFORMATION);
        SetHook();
        break;
    case DLL_PROCESS_DETACH:
        WriteLog("DLL detached from process");
        if (hOriginalDll)
            FreeLibrary(hOriginalDll);
        break;
    }
    return TRUE;
}