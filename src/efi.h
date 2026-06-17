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
    efi_event WaitForKey;
} efi_simple_text_input_protocol;

#define EFI_SIMPLE_TEXT_INPUT_GUID \
    {0x387477c1, 0x69c7, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

/* Simple Text Input Ex Protocol */
typedef struct _efi_simple_text_input_ex_protocol efi_simple_text_input_ex_protocol;
typedef efi_status (*efi_input_ex_read_key)(efi_simple_text_input_ex_protocol*, void*);
typedef efi_status (*efi_input_ex_set_state)(efi_simple_text_input_ex_protocol*, void*);
typedef efi_status (*efi_input_ex_register_key)(efi_simple_text_input_ex_protocol*, void*, void*, void*);
typedef efi_status (*efi_input_ex_unregister_key)(efi_simple_text_input_ex_protocol*, void*);

typedef struct {
    u16 ScanCode;
    char16 UnicodeChar;
    u32 ShiftState;
} efi_input_key_ex;

#define EFI_SHIFT_STATE_VALID     0x80000000
#define EFI_LEFT_SHIFT_PRESSED    0x00000001
#define EFI_RIGHT_SHIFT_PRESSED   0x00000002
#define EFI_LEFT_CONTROL_PRESSED  0x00000004
#define EFI_RIGHT_CONTROL_PRESSED 0x00000008
#define EFI_LEFT_ALT_PRESSED      0x00000010
#define EFI_RIGHT_ALT_PRESSED     0x00000020
#define EFI_LEFT_LOGO_PRESSED     0x00000040
#define EFI_RIGHT_LOGO_PRESSED    0x00000080

struct _efi_simple_text_input_ex_protocol {
    void *Reset;
    efi_input_ex_read_key  ReadKeyStrokeEx;
    efi_event WaitForKeyEx;
    efi_input_ex_set_state SetState;
    efi_input_ex_register_key RegisterKeyNotify;
    efi_input_ex_unregister_key UnregisterKeyNotify;
};

#define EFI_SIMPLE_TEXT_INPUT_EX_GUID \
    {0xdd9e7534, 0x7762, 0x4698, {0x8c,0x14,0xf5,0x85,0x17,0xa6,0x25,0xaa}}

/* ==================== Device Path Protocol ==================== */
typedef struct {
    u8 Type;
    u8 SubType;
    u16 Length;
} efi_device_path_protocol;

#define DEVICE_PATH_TYPE_MEDIA      4
#define DEVICE_PATH_TYPE_END        0x7F
#define MEDIA_FILEPATH_DP           4
#define END_ENTIRE_DEVICE_PATH      0xFF

#define EFI_DEVICE_PATH_PROTOCOL_GUID \
    {0x09576e91, 0x6d3f, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

/* ==================== Simple File System Protocol ==================== */
typedef struct _efi_file_protocol efi_file_protocol;

typedef efi_status (*efi_file_open)(efi_file_protocol*, efi_file_protocol**, char16*, u64, u64);
typedef efi_status (*efi_file_close)(efi_file_protocol*);
typedef efi_status (*efi_file_delete)(efi_file_protocol*);
typedef efi_status (*efi_file_read)(efi_file_protocol*, u64*, void*);
typedef efi_status (*efi_file_write)(efi_file_protocol*, u64*, void*);
typedef efi_status (*efi_file_get_position)(efi_file_protocol*, u64*);
typedef efi_status (*efi_file_set_position)(efi_file_protocol*, u64);
typedef efi_status (*efi_file_get_info)(efi_file_protocol*, efi_guid*, void*, u64*);
typedef efi_status (*efi_file_set_info)(efi_file_protocol*, efi_guid*, void*, u64);
typedef efi_status (*efi_file_flush)(efi_file_protocol*);

struct _efi_file_protocol {
    u64 Revision;
    efi_file_open Open;
    efi_file_close Close;
    efi_file_delete Delete;
    efi_file_read Read;
    efi_file_write Write;
    efi_file_get_position GetPosition;
    efi_file_set_position SetPosition;
    efi_file_get_info GetInfo;
    efi_file_set_info SetInfo;
    efi_file_flush Flush;
};

typedef struct {
    u64 Revision;
    efi_status (*OpenVolume)(void*, void**);
} efi_simple_file_system_protocol;

#define EFI_SIMPLE_FILE_SYSTEM_GUID \
    {0x964e5b22, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

#define EFI_FILE_MODE_READ      1
#define EFI_FILE_MODE_WRITE     2
#define EFI_FILE_MODE_CREATE    0x8000000000000000ULL

/* ==================== Loaded Image Protocol ==================== */
typedef struct {
    u32 Revision;
    efi_handle ParentHandle;
    efi_system_table *SystemTable;
    efi_handle DeviceHandle;
    void *FilePath;
    void *Reserved;
    u32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    u64 ImageSize;
    u64 ImageCodeType;
    u64 ImageDataType;
    efi_status (*Unload)(efi_handle);
} efi_loaded_image_protocol;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    {0x5B1B4A42, 0x46AE, 0x4D47, {0xA4,0xCD,0xC4,0x0E,0x86,0xD7,0xFB,0x87}}

/* ==================== Block I/O Protocol ==================== */
typedef struct {
    u32 MediaId;
    u8 RemovableMedia;
    u8 MediaPresent;
    u8 LogicalPartition;
    u8 ReadOnly;
    u8 WriteCaching;
    u8 Pad[3];
    u64 BlockSize;
    u64 LastBlock;
    u64 LowestAlignedLba;
    u32 LogicalBlocksPerPhysicalBlock;
    u32 OptimalTransferLengthGranularity;
} efi_block_io_media;

typedef struct {
    u64 Revision;
    efi_block_io_media *Media;
    efi_status (*Reset)(void*, u8);
    efi_status (*ReadBlocks)(void*, u32, u64, u64, void*);
    efi_status (*WriteBlocks)(void*, u32, u64, u64, void*);
    efi_status (*FlushBlocks)(void*);
} efi_block_io_protocol;

#define EFI_BLOCK_IO_GUID \
    {0x964e5b21, 0x6459, 0x11d2, {0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}}

/* ==================== Variable Services ==================== */
typedef efi_status (*efi_rt_get_variable)(char16*, efi_guid*, u32*, u64*, void*);
typedef efi_status (*efi_rt_set_variable)(char16*, efi_guid*, u32, u64, void*);

#define EFI_VARIABLE_NON_VOLATILE       0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS     0x00000004

#define EFI_GLOBAL_VARIABLE_GUID \
    {0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}}

/* ==================== Load Option (Boot Entry) ==================== */
typedef struct {
    u32 Attributes;
    u16 FilePathListLength;
} efi_load_option;

#define LOAD_OPTION_ACTIVE          0x00000001
#define LOAD_OPTION_CATEGORY_APP    0x00000800

/* ==================== Additional Boot Services typedefs ==================== */
typedef efi_status (*efi_bs_locate_handle_buffer)(u32, efi_guid*, void*, u64*, efi_handle**);

#define EFI_LOCATE_BY_PROTOCOL 2

/* Simple Pointer Protocol */
typedef struct {
    u64 InputReportWaitTimeout;
    u64 SampleCount;
    u32 MaximumPositiveX;
    u32 MaximumPositiveY;
    u32 MaximumPositiveZ;
    u32 MinimumNegativeX;
    u32 MinimumNegativeY;
    u32 MinimumNegativeZ;
} efi_simple_pointer_mode;

typedef struct _efi_simple_pointer_protocol {
    void *Reset;
    efi_status (*GetState)(struct _efi_simple_pointer_protocol*, void*);
    efi_event WaitForInput;
    efi_simple_pointer_mode *Mode;
} efi_simple_pointer_protocol;

typedef struct {
    i64 RelativeMovementX;
    i64 RelativeMovementY;
    i64 RelativeMovementZ;
    u32 Attributes;
    u32 Buttons;
} efi_simple_pointer_state;

#define EFI_SIMPLE_POINTER_PROTOCOL_GUID \
    {0x31878c87, 0xb75, 0x11d5, {0x9a,0x4f,0x00,0x90,0x27,0x3f,0xc1,0x4d}}

#define EFI_SIMPLE_POINTER_LEFT_BUTTON   0x01
#define EFI_SIMPLE_POINTER_RIGHT_BUTTON  0x02
#define EFI_SIMPLE_POINTER_MIDDLE_BUTTON 0x04

/* Absolute Pointer Protocol */
typedef struct _efi_absolute_pointer_protocol {
    void *Reset;
    efi_status (*GetState)(struct _efi_absolute_pointer_protocol*, void*);
    efi_event WaitForInput;
    void *Mode;
} efi_absolute_pointer_protocol;

#define EFI_ABSOLUTE_POINTER_PROTOCOL_GUID \
    {0x8D59D32B, 0xC655, 0x4AE9, {0x9B,0x15,0xF2,0x59,0x04,0x99,0x2A,0x43}}

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
