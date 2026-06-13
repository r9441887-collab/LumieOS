#ifndef __EFI_H__
#define __EFI_H__

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed long long i64;
typedef u64 usize;
typedef u64 uintn;
typedef u64 efi_status;
typedef void *efi_handle;
typedef void *efi_event;
typedef u16 char16;
typedef u8 boolean;

#define NULL ((void*)0)
#define TRUE 1
#define FALSE 0

#define EFI_SUCCESS 0
#define EFI_ERROR(s) ((i64)(s) < 0)
#define EFI_ERR(x) (x | (1ULL << 63))
#define EFI_INVALID_PARAMETER EFI_ERR(2)
#define EFI_NOT_FOUND EFI_ERR(14)

#define SIGNATURE_16(a,b) ((a) | ((b) << 8))
#define SIGNATURE_32(a,b,c,d) (SIGNATURE_16(a,b) | (SIGNATURE_16(c,d) << 16))
#define SIGNATURE_64(a,b,c,d,e,f,g,h) (SIGNATURE_32(a,b,c,d) | ((u64)SIGNATURE_32(e,f,g,h) << 32))

typedef struct {
    u32 Data1;
    u16 Data2;
    u16 Data3;
    u8  Data4[8];
} efi_guid;

typedef struct {
    u64 Signature;
    u32 Revision;
    u32 HeaderSize;
    u32 CRC32;
    u32 Reserved;
} efi_table_header;

typedef struct {
    efi_table_header Hdr;
    char16 *FirmwareVendor;
    u32 FirmwareRevision;
    efi_handle ConsoleInHandle;
    void *ConIn;
    efi_handle ConsoleOutHandle;
    void *ConOut;
    efi_handle StandardErrorHandle;
    void *StdErr;
    void *RuntimeServices;
    void *BootServices;
    u64 NumberOfTableEntries;
    void *ConfigurationTable;
} efi_system_table;

typedef struct {
    efi_table_header Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    void *AllocatePages;
    void *FreePages;
    void *GetMemoryMap;
    void *AllocatePool;
    void *FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    void *ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    void *LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} efi_boot_services;

typedef struct {
    u32 Type;
    u32 Padding;
    u64 PhysicalStart;
    u64 VirtualStart;
    u64 NumberOfPages;
    u64 Attribute;
} efi_memory_descriptor;

#define EFI_MEMORY_DESCRIPTOR_VERSION 1
#define EFI_BOOT_SERVICES_DATA 4

typedef struct {
    u32 RedMask;
    u32 GreenMask;
    u32 BlueMask;
    u32 ReservedMask;
} efi_pixel_bitmask;

typedef struct {
    u32 Version;
    u32 HorizontalResolution;
    u32 VerticalResolution;
    u32 PixelFormat;
    efi_pixel_bitmask PixelInformation;
    u32 PixelsPerScanLine;
} efi_gop_mode_info;

#define EFI_GOP_PIXEL_RGBX_8BPP 0
#define EFI_GOP_PIXEL_BGRX_8BPP 1

typedef efi_status (*efi_gop_query_mode)(void*, u32, u64*, efi_gop_mode_info**);
typedef efi_status (*efi_gop_set_mode)(void*, u32);
typedef efi_status (*efi_gop_blt)(void*, void*, u32, u32, u32, u32, u32, u32, u32, u32);

typedef struct {
    u32               MaxMode;
    u32               Mode;
    efi_gop_mode_info *Info;
    u64               SizeOfInfo;
    u64               FrameBufferBase;
    u64               FrameBufferSize;
} efi_gop_mode;

typedef struct {
    efi_gop_query_mode QueryMode;
    efi_gop_set_mode   SetMode;
    efi_gop_blt        Blt;
    efi_gop_mode       *Mode;
} efi_gop_protocol;

#define EFI_GOP_GUID \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96,0xfb,0x7a,0xde,0xd0,0x80,0x51,0x6a}}

typedef struct {
    u16 ScanCode;
    char16 UnicodeChar;
} efi_input_key;

typedef efi_status (*efi_input_read_key)(void*, efi_input_key*);
typedef void (*efi_input_reset)(void*, boolean);

typedef struct {
    efi_input_reset  Reset;
    efi_input_read_key ReadKeyStroke;
    void *WaitForKey;
} efi_simple_text_input_protocol;

#define EFI_SIMPLE_TEXT_INPUT_GUID \
    {0x387477c1, 0x69c7, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

typedef efi_status (*efi_bs_locate_protocol)(efi_guid*, void*, void**);
typedef efi_status (*efi_bs_handle_protocol)(efi_handle, efi_guid*, void**);
typedef efi_status (*efi_bs_exit_boot_services)(efi_handle, u64);
typedef efi_status (*efi_bs_allocate_pool)(u32, u64, void**);
typedef efi_status (*efi_bs_free_pool)(void*);
typedef efi_status (*efi_bs_create_event)(u32, u64, void*, void*, efi_event*);
typedef efi_status (*efi_bs_close_event)(efi_event);
typedef efi_status (*efi_bs_wait_for_event)(u64, efi_event*, u64*);
typedef void (*efi_bs_copy_mem)(void*, void*, u64);
typedef void (*efi_bs_set_mem)(void*, u64, u8);
typedef efi_status (*efi_bs_stall)(u64);

typedef efi_status (*efi_out_output_string)(void*, char16*);
typedef void (*efi_out_reset)(void*, boolean);
typedef efi_status (*efi_out_set_attribute)(void*, u64);
typedef efi_status (*efi_out_clear_screen)(void*);
typedef efi_status (*efi_out_set_cursor_position)(void*, u64, u64);
typedef efi_status (*efi_out_enable_cursor)(void*, boolean);

typedef struct {
    efi_out_reset             Reset;
    efi_out_output_string     OutputString;
    void                      *TestString;
    efi_out_set_attribute     SetAttribute;
    efi_out_clear_screen      ClearScreen;
    efi_out_set_cursor_position SetCursorPosition;
    efi_out_enable_cursor     EnableCursor;
    void                      *Mode;
} efi_simple_text_output_protocol;

#define EFI_TEXT_ATTR(fg,bg) ((fg) | ((bg) << 4))
#define EFI_BLACK   0
#define EFI_BLUE    1
#define EFI_GREEN   2
#define EFI_CYAN    3
#define EFI_RED     4
#define EFI_MAGENTA 5
#define EFI_BROWN   6
#define EFI_LIGHTGRAY 7
#define EFI_BRIGHT  8

/* ==================== Varargs ==================== */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v,l) __builtin_va_arg(v,l)

/* ==================== Runtime Services ==================== */
typedef enum {
    EfiResetCold,
    EfiResetWarm,
    EfiResetShutdown
} efi_reset_type;

typedef void (*efi_rt_reset_system)(efi_reset_type, efi_status, u64, void*);

typedef struct {
    efi_table_header Hdr;
    void *GetTime;
    void *SetTime;
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    void *GetVariable;
    void *GetNextVariableName;
    void *SetVariable;
    void *GetNextHighMonotonicCount;
    void *ResetSystem;
    void *UpdateCapsule;
    void *QueryCapsuleCapabilities;
    void *QueryVariableInfo;
} efi_runtime_services;

#endif
