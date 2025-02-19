#include <windows.h>
#include <cstdint>
#include <string>
#include <Psapi.h>

// Сигнатура для поиска функции
const unsigned char FUNCTION_SIGNATURE[] = {
    0x55,                   // push ebp
    0x8B, 0xEC,            // mov ebp, esp
    0x81, 0xEC, 0x84, 0x00, 0x00, 0x00,  // sub esp, 0x84
    0xA1, 0x00, 0x00, 0x00, 0x00,        // mov eax, dword [__security_cookie] (адрес пропускаем)
    0x33, 0xC5,            // xor eax, ebp
    0x89, 0x45, 0xFC,      // mov dword [ebp-0x4], eax
    0x53,                   // push ebx
    0x6A, 0x7F,            // push 0x7f
    0x8D, 0x85, 0x7D, 0xFF, 0xFF, 0xFF,  // lea eax, [ebp-0x83]
    0x6A, 0x00,            // push 0x0
    0x50,                   // push eax
    0xC6, 0x00, 0x00, 0x00 // mov byte [ebp-0x84], 0x0 (исправлено)
};

// Маска для сигнатуры: x - проверять байт, ? - пропустить байт
const char SIGNATURE_MASK[] =
"x"           // push ebp + mov ebp, esp (3)
"xx"
"xxxxxx"     // sub esp, 0x84 (9)
"x????"         // mov eax, dword [__security_cookie] (5)
"xx"            // xor eax, ebp (2)
"xxx"           // mov dword [ebp-0x4], eax (3)
"x"             // push ebx (1)
"xx"            // push 0x7f (2)
"xxxxxx"        // lea eax, [ebp-0x83] (6)
"xx"            // push 0x0 (2)
"x"             // push eax (1)
"x???";         // mov byte [ebp-0x84], 0x0 (4)

const size_t SIGNATURE_SIZE = sizeof(FUNCTION_SIGNATURE);

// Адрес оригинальной функции (будет найден по сигнатуре)
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

// Поиск сигнатуры в памяти с учетом маски
uintptr_t FindSignature(uintptr_t start, size_t size)
{
    const size_t maskLength = strlen(SIGNATURE_MASK);
    if (maskLength != SIGNATURE_SIZE)
    {
        char message[256];
        sprintf_s(message, "Signature size mismatch! Mask length: %d, Signature size: %d", maskLength, SIGNATURE_SIZE);
        WriteLog(message);
        return 0;
    }

    for (size_t i = 0; i < size - SIGNATURE_SIZE; i++)
    {
        bool found = true;
        for (size_t j = 0; j < SIGNATURE_SIZE; j++)
        {
            if (SIGNATURE_MASK[j] == 'x' && ((unsigned char*)start)[i + j] != FUNCTION_SIGNATURE[j])
            {
                found = false;
                break;
            }
        }
        if (found)
        {
            char message[256];
            sprintf_s(message, "Found signature at offset: 0x%X", i);
            WriteLog(message);
            // Добавим вывод первых байтов найденной сигнатуры для проверки
            unsigned char foundBytes[10];
            memcpy(foundBytes, (void*)(start + i), 10);
            sprintf_s(message, "Found bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                foundBytes[0], foundBytes[1], foundBytes[2], foundBytes[3], foundBytes[4],
                foundBytes[5], foundBytes[6], foundBytes[7], foundBytes[8], foundBytes[9]);
            WriteLog(message);
            return start + i;
        }
    }
    WriteLog("Signature not found in memory");
    return 0;
}

// Инициализация адресов функций
bool InitializeFunctions()
{
    char message[256];

    // Получаем информацию о модуле
    HMODULE hModule = GetModuleHandleA(NULL);
    if (!hModule)
    {
        WriteLog("Failed to get module handle");
        return false;
    }

    MODULEINFO moduleInfo;
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo)))
    {
        WriteLog("Failed to get module information");
        return false;
    }

    // Ищем сигнатуру в памяти модуля
    originalFunctionAddress = FindSignature((uintptr_t)moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage);
    if (!originalFunctionAddress)
    {
        WriteLog("Failed to find function signature");
        return false;
    }

    sprintf_s(message, "Found function at address: 0x%p", (void*)originalFunctionAddress);
    WriteLog(message);

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

    return foundCalls >= 4;
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