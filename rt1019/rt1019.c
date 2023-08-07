#include "rt1019.h"
#include "registers.h"

#define bool int

static ULONG Rt1019DebugLevel = 100;
static ULONG Rt1019DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	Rt1019Print(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Rt1019EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		Rt1019Print(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

static NTSTATUS rt1019_reg_write(PRT1019_CONTEXT pDevice, uint16_t reg, uint8_t data)
{
	uint8_t rawdata[3];
	rawdata[0] = (reg >> 8) & 0xff;
	rawdata[1] = (reg & 0xff);
	rawdata[2] = data;
	return SpbWriteDataSynchronously(&pDevice->I2CContext, rawdata, sizeof(rawdata));
}

struct reg {
	UINT16 reg;
	UINT8 val;
};

static NTSTATUS rt1019_reg_read(PRT1019_CONTEXT pDevice, uint16_t reg, uint8_t* data)
{
	uint16_t reg_swap = RtlUshortByteSwap(reg);
	NTSTATUS ret = SpbXferDataSynchronously(&pDevice->I2CContext, &reg_swap, sizeof(uint16_t), data, sizeof(uint8_t));
	return ret;
}

static NTSTATUS rt1019_reg_burstWrite(PRT1019_CONTEXT pDevice, struct reg* regs, int regCount) {
	NTSTATUS status = STATUS_NO_MEMORY;
	for (int i = 0; i < regCount; i++) {
		struct reg* regToSet = &regs[i];
		status = rt1019_reg_write(pDevice, regToSet->reg, regToSet->val);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}
	return status;
}

NTSTATUS
GetDeviceUID(
	_In_ WDFDEVICE FxDevice,
	_In_ PINT32 PUID
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_UID"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	uint32_t uid;
	if (outputBuffer->Argument[0].DataLength >= 4) {
		uid = *(uint32_t*)outputBuffer->Argument->Data;
	}
	else if (outputBuffer->Argument[0].DataLength >= 2) {
		uid = *(uint16_t*)outputBuffer->Argument->Data;
	}
	else {
		uid = *(uint8_t*)outputBuffer->Argument->Data;
	}
	if (PUID) {
		*PUID = uid;
	}
	else {
		status = STATUS_ACPI_INVALID_ARGUMENT;
	}
Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

NTSTATUS
StartCodec(
	PRT1019_CONTEXT pDevice
) {
	NTSTATUS status = STATUS_SUCCESS;
	if (!pDevice->SetUID) {
		status = STATUS_ACPI_INVALID_DATA;
		return status;
	}

	
	uint8_t val = 0;
	uint8_t val2 = 0;
	rt1019_reg_read(pDevice, RT1019_DEV_ID_1, &val);
	rt1019_reg_read(pDevice, RT1019_DEV_ID_2, &val2);
	uint16_t dev_id = val << 8 | val2;
	if ((dev_id != RT1019_DEVICE_ID_VAL) && (dev_id != RT1019_DEVICE_ID_VAL2)) {
		return STATUS_INVALID_DEVICE_STATE;
	}

	//left/right diffs
	// RT1019_IDS_CTRL

	struct reg regsCommon[] = {
		{RT1019_PWR_STRP_2, 0x04},
		{RT1019_SDB_CTRL, 0x0b},
		{RT1019_CLK_TREE_1, 0x80},
		{RT1019_CLK_TREE_2, 0x50},
		{RT1019_CLK_TREE_3, 0x23},
		{RT1019_PLL_1, 0x02},
		{RT1019_PLL_2, 0x0a},
		{RT1019_PLL_3, 0x84},
		{RT1019_TDM_1, 0x00},
		{RT1019_TDM_2, 0xd0},
		{RT1019_TDM_3, 0x23}
	};

	status = rt1019_reg_burstWrite(pDevice, regsCommon, sizeof(regsCommon) / sizeof(struct reg));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (pDevice->UID == 0) {
		status = rt1019_reg_write(pDevice, RT1019_IDS_CTRL, 0x04);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}
	else if (pDevice->UID == 1) {
		status = rt1019_reg_write(pDevice, RT1019_IDS_CTRL, 0x05);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	pDevice->DevicePoweredOn = TRUE;
	return status;
}

NTSTATUS
StopCodec(
	PRT1019_CONTEXT pDevice
) {
	NTSTATUS status;

	status = rt1019_reg_write(pDevice, RT1019_RESET, 0);

	pDevice->DevicePoweredOn = FALSE;
	return status;
}

int CsAudioArg2 = 1;

VOID
CSAudioRegisterEndpoint(
	PRT1019_CONTEXT pDevice
) {
	CsAudioArg arg;
	RtlZeroMemory(&arg, sizeof(CsAudioArg));
	arg.argSz = sizeof(CsAudioArg);
	arg.endpointType = CSAudioEndpointTypeSpeaker;
	arg.endpointRequest = CSAudioEndpointRegister;
	ExNotifyCallback(pDevice->CSAudioAPICallback, &arg, &CsAudioArg2);
}

VOID
CsAudioCallbackFunction(
	IN PRT1019_CONTEXT pDevice,
	CsAudioArg* arg,
	PVOID Argument2
) {
	if (!pDevice) {
		return;
	}

	if (Argument2 == &CsAudioArg2) {
		return;
	}

	pDevice->CSAudioManaged = TRUE;

	CsAudioArg localArg;
	RtlZeroMemory(&localArg, sizeof(CsAudioArg));
	RtlCopyMemory(&localArg, arg, min(arg->argSz, sizeof(CsAudioArg)));

	if (localArg.endpointType == CSAudioEndpointTypeDSP && localArg.endpointRequest == CSAudioEndpointRegister) {
		CSAudioRegisterEndpoint(pDevice);
	}
	else if (localArg.endpointType != CSAudioEndpointTypeSpeaker) {
		return;
	}

	if (localArg.endpointRequest == CSAudioEndpointStop) {
		StopCodec(pDevice);
	}
	if (localArg.endpointRequest == CSAudioEndpointStart) {
		StartCodec(pDevice);
	}
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRT1019_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = GetDeviceUID(FxDevice, &pDevice->UID);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	pDevice->SetUID = TRUE;

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRT1019_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	if (pDevice->CSAudioAPICallbackObj) {
		ExUnregisterCallback(pDevice->CSAudioAPICallbackObj);
		pDevice->CSAudioAPICallbackObj = NULL;
	}

	if (pDevice->CSAudioAPICallback) {
		ObfDereferenceObject(pDevice->CSAudioAPICallback);
		pDevice->CSAudioAPICallback = NULL;
	}

	return status;
}

NTSTATUS
OnSelfManagedIoInit(
	_In_
	WDFDEVICE FxDevice
) {
	PRT1019_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	// CS Audio Callback

	UNICODE_STRING CSAudioCallbackAPI;
	RtlInitUnicodeString(&CSAudioCallbackAPI, L"\\CallBack\\CsAudioCallbackAPI");


	OBJECT_ATTRIBUTES attributes;
	InitializeObjectAttributes(&attributes,
		&CSAudioCallbackAPI,
		OBJ_KERNEL_HANDLE | OBJ_OPENIF | OBJ_CASE_INSENSITIVE | OBJ_PERMANENT,
		NULL,
		NULL
	);
	status = ExCreateCallback(&pDevice->CSAudioAPICallback, &attributes, TRUE, TRUE);
	if (!NT_SUCCESS(status)) {

		return status;
	}

	pDevice->CSAudioAPICallbackObj = ExRegisterCallback(pDevice->CSAudioAPICallback,
		CsAudioCallbackFunction,
		pDevice
	);
	if (!pDevice->CSAudioAPICallbackObj) {

		return STATUS_NO_CALLBACK_ACTIVE;
	}

	CSAudioRegisterEndpoint(pDevice);

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRT1019_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = StartCodec(pDevice);

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRT1019_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = StopCodec(pDevice);

	return STATUS_SUCCESS;
}

NTSTATUS
Rt1019EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDFQUEUE                      queue;
	PRT1019_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	Rt1019Print(DEBUG_LEVEL_INFO, DBG_PNP,
		"Rt1019EvtDeviceAdd called\n");

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceSelfManagedIoInit = OnSelfManagedIoInit;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RT1019_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		Rt1019Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = Rt1019EvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		Rt1019Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		Rt1019Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	return status;
}

VOID
Rt1019EvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PRT1019_CONTEXT     devContext;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	switch (IoControlCode)
	{
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	WdfRequestComplete(Request, status);

	return;
}