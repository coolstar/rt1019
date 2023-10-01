#if !defined(_RT1019_H_)
#define _RT1019_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#include <acpiioct.h>
#include <ntstrsafe.h>

#include <stdint.h>

#include "spb.h"

//
// String definitions
//

#define DRIVERNAME                 "rt1019.sys: "

#define RT1019_POOL_TAG            (ULONG) '9101'

#define true 1
#define false 0

typedef enum platform {
	PlatformNone,
	PlatformRyzenPicassoDali,
	PlatformRyzenCezanne,
	PlatformRyzenMendocino,
	PlatformGeminiLake,
	PlatformCometLake,
	PlatformTigerLake
} Platform;

typedef enum {
	CSAudioEndpointTypeDSP,
	CSAudioEndpointTypeSpeaker,
	CSAudioEndpointTypeHeadphone,
	CSAudioEndpointTypeMicArray,
	CSAudioEndpointTypeMicJack
} CSAudioEndpointType;

typedef enum {
	CSAudioEndpointRegister,
	CSAudioEndpointStart,
	CSAudioEndpointStop,
	CSAudioEndpointOverrideFormat
} CSAudioEndpointRequest;

typedef struct CSAUDIOFORMATOVERRIDE {
	UINT16 channels;
	UINT16 frequency;
	UINT16 bitsPerSample;
	UINT16 validBitsPerSample;
	BOOLEAN force32BitOutputContainer;
} CsAudioFormatOverride;

typedef struct CSAUDIOARG {
	UINT32 argSz;
	CSAudioEndpointType endpointType;
	CSAudioEndpointRequest endpointRequest;
	union {
		CsAudioFormatOverride formatOverride;
	};
} CsAudioArg, * PCsAudioArg;

typedef struct _RT1019_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	SPB_CONTEXT I2CContext;

	BOOLEAN SetUID;
	INT32 UID;

	BOOLEAN DevicePoweredOn;

	PCALLBACK_OBJECT CSAudioAPICallback;
	PVOID CSAudioAPICallbackObj;

	BOOLEAN CSAudioManaged;
	BOOLEAN CSAudioRequestsOn;

} RT1019_CONTEXT, *PRT1019_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RT1019_CONTEXT, GetDeviceContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD Rt1019DriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD Rt1019EvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS Rt1019EvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL Rt1019EvtInternalDeviceControl;

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define Rt1019Print(dbglevel, dbgcatagory, fmt, ...) {          \
    if (Rt1019DebugLevel >= dbglevel &&                         \
        (Rt1019DebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define Rt1019Print(dbglevel, fmt, ...) {                       \
}
#endif

#endif