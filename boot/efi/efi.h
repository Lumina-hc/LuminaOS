#ifndef LUMINA_EFI_H
#define LUMINA_EFI_H

#include <stdint.h>
#include <stddef.h>

typedef uint64_t EFI_STATUS;
typedef void* EFI_HANDLE;
typedef void* EFI_EVENT;
typedef uint16_t CHAR16;
typedef uint64_t EFI_PHYSICAL_ADDRESS;

#define EFI_SUCCESS           0
#define EFI_ERROR(s)          (((int64_t)(s)) < 0)

#define EFI_OPEN_PROTOCOL_GET_PROTOCOL 0x02
#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_ALLOCATE_ANY_PAGES 0
#define EfiLoaderData         2

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (*Reset)(void* This, int ExtendedVerification, void* Mode);
    EFI_STATUS (*OutputString)(void* This, CHAR16* String);
    EFI_STATUS (*TestString)(void* This, CHAR16* String);
    EFI_STATUS (*QueryMode)(void* This, uint64_t ModeNumber, uint64_t* Columns, uint64_t* Rows);
    EFI_STATUS (*SetMode)(void* This, uint64_t ModeNumber);
    EFI_STATUS (*SetAttribute)(void* This, uint64_t Attribute);
    EFI_STATUS (*ClearScreen)(void* This);
    EFI_STATUS (*SetCursorPosition)(void* This, uint64_t Column, uint64_t Row);
    EFI_STATUS (*EnableCursor)(void* This, int Visible);
    void* Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    uint32_t FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    void* RuntimeServices;
    void* BootServices;
    uint64_t NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    uint64_t* RaiseTPL;
    uint64_t* RestoreTPL;
    EFI_STATUS (*AllocatePages)(int Type, int MemoryType, uint64_t Pages, EFI_PHYSICAL_ADDRESS* Memory);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS Memory, uint64_t Pages);
    void* GetMemoryMap;
    EFI_STATUS (*AllocatePool)(int PoolType, uint64_t Size, void** Buffer);
    EFI_STATUS (*FreePool)(void* Buffer);
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface);
    void* PCHandleProtocol;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* ImageLoad;
    void* ImageStart;
    void* Exit;
    void* ImageUnload;
    void* ExitBootServices;
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    EFI_STATUS (*OpenProtocol)(EFI_HANDLE Handle, EFI_GUID* Protocol, void** Interface,
                               EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, uint32_t Attributes);
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    EFI_STATUS (*LocateHandleBuffer)(int SearchType, EFI_GUID* Protocol, void* SearchKey,
                                     uint64_t* NoHandles, EFI_HANDLE** Buffer);
    void* LocateProtocol;
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (*Open)(void* This, void** NewHandle, CHAR16* FileName, uint64_t OpenMode, uint64_t Attributes);
    EFI_STATUS (*Close)(void* This);
    EFI_STATUS (*Delete)(void* This);
    EFI_STATUS (*Read)(void* This, uint64_t* BufferSize, void* Buffer);
    EFI_STATUS (*Write)(void* This, uint64_t* BufferSize, void* Buffer);
    EFI_STATUS (*GetPosition)(void* This, uint64_t* Position);
    EFI_STATUS (*SetPosition)(void* This, uint64_t Position);
    EFI_STATUS (*GetInfo)(void* This, EFI_GUID* InformationType, uint64_t* BufferSize, void* Buffer);
    EFI_STATUS (*SetInfo)(void* This, EFI_GUID* InformationType, uint64_t BufferSize, void* Buffer);
    EFI_STATUS (*Flush)(void* This);
} EFI_FILE_PROTOCOL;

typedef struct {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(void* This, EFI_FILE_PROTOCOL** Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME CreationTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    uint64_t Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    uint32_t PixelRedMask;
    uint32_t PixelGreenMask;
    uint32_t PixelBlueMask;
    uint32_t PixelReservedMask;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    uint64_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    EFI_STATUS (*QueryMode)(void* This, uint32_t ModeNumber, uint64_t* SizeOfInfo,
                            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);
    EFI_STATUS (*SetMode)(void* This, uint32_t ModeNumber);
    EFI_STATUS (*Blt)(void* This, void* BltBuffer, uint32_t BltOperation,
                      uint64_t SourceX, uint64_t SourceY, uint64_t DestinationX,
                      uint64_t DestinationY, uint64_t Width, uint64_t Height, uint64_t Delta);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

static const EFI_GUID LUMINA_SFS_GUID  = {0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID LUMINA_FILE_GUID = {0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
static const EFI_GUID LUMINA_GOP_GUID  = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

#define LUMINA_BOOT_INFO_ADDR 0x6000ULL
#define LUMINA_BOOT_MAGIC     0x4C554D424F4F5401ULL

typedef struct {
    uint64_t Magic;
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
    uint32_t Width;
    uint32_t Height;
    uint32_t Stride;
    uint32_t PixelFormat;
} LUMINA_BOOT_INFO;

#endif