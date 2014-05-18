
/*++

Module Name:

    PciDrv.c

Abstract:

    This driver can be installed as a standalone driver (genpci.inf)
    for the Intel PCI device or can be used as a network driver by using
    NDIS-WDM driver sample (netdrv.inf) from the DDK as an upper
    device filter. Please read the PCIDRV.HTM file for more information.

Environment:

    Kernel mode

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
//
// The trace message header (.tmh) file must be included in a source file
// before any WPP macro calls and after defining a WPP_CONTROL_GUIDS
// macro (defined in toaster.h). During the compilation, WPP scans the source
// files for DoTraceMessage() calls and builds a .tmh file which stores a unique
// data GUID for each message, the text resource string for each message,
// and the data types of the variables passed in for each message.  This file
// is automatically generated and used during post-processing.
//
#include "pcidrv.tmh"
#endif

//
// Global debug error level
//
#if !defined(EVENT_TRACING)
ULONG DebugLevel = TRACE_LEVEL_INFORMATION;
ULONG DebugFlag = 0x2f;//0x46;//0x4FF; //0x00000006;
#else
ULONG DebugLevel; // wouldn't be used to control the TRACE_LEVEL_VERBOSE
ULONG DebugFlag;
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, PciDrvEvtDeviceAdd)
#pragma alloc_text (PAGE, PciDrvEvtDeviceContextCleanup)
#pragma alloc_text (PAGE, PciDrvReadRegistryValue)
#pragma alloc_text (PAGE, PciDrvWriteRegistryValue)
#pragma alloc_text (PAGE, PciDrvEvtDriverContextCleanup)
#endif


#define PARAMATER_NAME_LEN 80

NTSTATUS
DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:

    Installable driver initialization entry point.
    This entry point is called directly by the I/O system.

Arguments:

    DriverObject - pointer to the driver object

    RegistryPath - pointer to a unicode string representing the path,
                   to driver-specific key in the registry.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    NTSTATUS               status = STATUS_SUCCESS;
    WDF_DRIVER_CONFIG      config;
    WDF_OBJECT_ATTRIBUTES  attrib;
    WDFDRIVER              driver;
    PDRIVER_CONTEXT        driverContext;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING( DriverObject, RegistryPath );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "PCIDRV Sample - Driver Framework Edition \n");
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "Built %s %s\n", __DATE__, __TIME__);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrib, DRIVER_CONTEXT);

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    attrib.EvtCleanupCallback = PciDrvEvtDriverContextCleanup;

    //
    // Initialize the Driver Config structure..
    //
    WDF_DRIVER_CONFIG_INIT(&config, PciDrvEvtDeviceAdd);

    //
    // Create a WDFDRIVER object.
    //
    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attrib,
                             &config,
                             &driver);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "WdfDriverCreate failed with status %!STATUS!\n", status);
        //
        // Cleanup tracing here because DriverContextCleanup will not be called
        // as we have failed to create WDFDRIVER object itself.
        // Please note that if your return failure from DriverEntry after the
        // WDFDRIVER object is created successfully, you don't have to
        // call WPP cleanup because in those cases DriverContextCleanup
        // will be executed when the framework deletes the DriverObject.
        //
        WPP_CLEANUP(DriverObject);
        return status;
    }

    driverContext = GetDriverContext(driver);

    //
    // Create a driver wide lookside list used for allocating memory  for the
    // MP_RFD structure for all device instances (if there are multiple present).
    //
    status = WdfLookasideListCreate(WDF_NO_OBJECT_ATTRIBUTES, // LookAsideAttributes
                                sizeof(MP_RFD),
                                NonPagedPool,
                                WDF_NO_OBJECT_ATTRIBUTES, // MemoryAttributes
                                PCIDRV_POOL_TAG,
                                &driverContext->RecvLookaside
                                );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "Couldn't allocate lookaside list status %!STATUS!\n", status);
        return status;
    }

    return status;

}

NTSTATUS
PciDrvEvtDeviceAdd(
    IN WDFDRIVER        Driver,
    IN PWDFDEVICE_INIT  DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS                        status = STATUS_SUCCESS;
    WDF_OBJECT_ATTRIBUTES           fdoAttributes;
    WDFDEVICE                       device;
    PFDO_DATA                       fdoData = NULL;
    ULONG                           isUpperEdgeNdis;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "-->PciDrvEvtDeviceAdd routine. Driver: 0x%p\n", Driver);

    //
    // I/O type is Buffered by default. If required to use something else,
    // call WdfDeviceInitSetIoType with the appropriate type.
    //
    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

    //
    // Specify the context type and size for the device we are about to create.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&fdoAttributes, FDO_DATA);

    //
    // ContextCleanup will be called by the framework when it deletes the device.
    // So you can defer freeing any resources allocated to Cleanup callback in the
    // event AddDevice returns any error after the device is created.
    //
    fdoAttributes.EvtCleanupCallback = PciDrvEvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &fdoAttributes, &device);

    if ( !NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfDeviceInitialize failed %!STATUS!\n", status);
        return status;
    }

    //
    // Device creation is complete.
    // Get the DeviceExtension and initialize it.
    //
    fdoData = FdoGetData(device);
    fdoData->WdfDevice = device;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "PDO(0x%p) FDO(0x%p), Lower(0x%p) DevExt (0x%p)\n",
                WdfDeviceWdmGetPhysicalDevice (device),
                WdfDeviceWdmGetDeviceObject (device),
                WdfDeviceWdmGetAttachedDevice(device),
                fdoData);

    //
    // Initialize the device extension and allocate all the software resources
    //
    status = NICAllocateSoftwareResources(fdoData);
    if (!NT_SUCCESS (status)){
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "NICAllocateSoftwareResources failed: %!STATUS!\n",
                    status);
        return status;
    }

    //
    // Tell the Framework that this device will need an interface so that
    // application can interact with it.
    //
    status = WdfDeviceCreateDeviceInterface(
                 device,
                 (LPGUID) &GUID_DEVINTERFACE_PCIDRV,
                 NULL
             );

    if (!NT_SUCCESS (status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "WdfDeviceCreateDeviceInterface failed %!STATUS!\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- PciDrvEvtDeviceAdd  \n");

    return status;
}

VOID
PciDrvEvtDeviceContextCleanup (
    WDFDEVICE       Device
    )
/*++

Routine Description:

   EvtDeviceContextCleanup event callback cleans up anything done in
   EvtDeviceAdd, except those things that are automatically cleaned
   up by the Framework.

   In the case of this sample, everything is automatically handled.  In a
   driver derived from this sample, it's quite likely that this function could
   be deleted.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    VOID

--*/
{
    PFDO_DATA               fdoData = NULL;
    NTSTATUS                status;

    PAGED_CODE();

    fdoData = FdoGetData(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "--> PciDrvEvtDeviceContextCleanup\n");

    status = NICFreeSoftwareResources(fdoData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "<-- PciDrvEvtDeviceContextCleanup\n");

}

NTSTATUS
PciDrvEvtDevicePrepareHardware (
    WDFDEVICE      Device,
    WDFCMRESLIST   Resources,
    WDFCMRESLIST   ResourcesTranslated
    )
/*++

Routine Description:

    EvtDeviceStart event callback performs operations that are necessary
    to make the driver's device operational. The framework calls the driver's
    EvtDeviceStart callback when the PnP manager sends an IRP_MN_START_DEVICE
    request to the driver stack.

Arguments:

    Device - Handle to a framework device object.

    Resources - Handle to a collection of framework resource objects.
                This collection identifies the raw (bus-relative) hardware
                resources that have been assigned to the device.

    ResourcesTranslated - Handle to a collection of framework resource objects.
                This collection identifies the translated (system-physical)
                hardware resources that have been assigned to the device.
                The resources appear from the CPU's point of view.
                Use this list of resources to map I/O space and
                device-accessible memory into virtual address space

Return Value:

    WDF status code

--*/
{
    NTSTATUS     status = STATUS_SUCCESS;
    PFDO_DATA    fdoData = NULL;

    UNREFERENCED_PARAMETER(Resources);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "--> PciDrvEvtDevicePrepareHardware\n");

    fdoData = FdoGetData(Device);

    status = NICMapHWResources(fdoData, ResourcesTranslated);
    if (!NT_SUCCESS (status)){
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                    "NICMapHWResources failed: %!STATUS!\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "<-- PciDrvEvtDevicePrepareHardware\n");

    return status;

}

NTSTATUS
PciDrvEvtDeviceReleaseHardware(
    IN  WDFDEVICE    Device,
    IN  WDFCMRESLIST ResourcesTranslated
    )
/*++

Routine Description:

    EvtDeviceReleaseHardware is called by the framework whenever the PnP manager
    is revoking ownership of our resources.  This may be in response to either
    IRP_MN_STOP_DEVICE or IRP_MN_REMOVE_DEVICE.  The callback is made before
    passing down the IRP to the lower driver.

    In this callback, do anything necessary to free those resources.

Arguments:

    Device - Handle to a framework device object.

    ResourcesTranslated - Handle to a collection of framework resource objects.
                This collection identifies the translated (system-physical)
                hardware resources that have been assigned to the device.
                The resources appear from the CPU's point of view.
                Use this list of resources to map I/O space and
                device-accessible memory into virtual address space

Return Value:

    NTSTATUS - Failures will be logged, but not acted on.

--*/
{
    PFDO_DATA  fdoData = NULL;

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "--> PciDrvEvtDeviceReleaseHardware\n");

    fdoData = FdoGetData(Device);

    //
    // Unmap any I/O ports. Disconnecting from the interrupt will be done
    // automatically by the framework.
    //
    NICUnmapHWResources(fdoData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
                "<-- PciDrvEvtDeviceReleaseHardware\n");

    return STATUS_SUCCESS;
}


BOOLEAN
PciDrvReadRegistryValue(
    __in  PFDO_DATA   FdoData,
    __in  PWCHAR      Name,
    __out PULONG      Value
    )
/*++

Routine Description:

    Can be used to read any REG_DWORD registry value stored
    under Device Parameter.

Arguments:

    FdoData - pointer to the device extension
    Name - Name of the registry value
    Value -


Return Value:

   TRUE if successful
   FALSE if not present/error in reading registry

--*/
{
    WDFKEY      hKey = NULL;
    NTSTATUS    status;
    BOOLEAN     retValue = FALSE;
    UNICODE_STRING  valueName;



    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "-->PciDrvReadRegistryValue \n");

    *Value = 0;

    status = WdfDeviceOpenRegistryKey(FdoData->WdfDevice,
                                      PLUGPLAY_REGKEY_DEVICE,
                                      STANDARD_RIGHTS_ALL,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &hKey);

    if (NT_SUCCESS (status)) {

        RtlInitUnicodeString(&valueName,Name);

        status = WdfRegistryQueryULong( hKey,
                                        &valueName,
                                        Value );

        if (NT_SUCCESS (status)) {
            retValue = TRUE;
        }

        WdfRegistryClose(hKey);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "<--PciDrvReadRegistryValue %ws %d \n", Name, *Value);

    return retValue;
}

BOOLEAN
PciDrvWriteRegistryValue(
    __in PFDO_DATA  FdoData,
    __in PWCHAR     Name,
    __in ULONG      Value
    )
/*++

Routine Description:

    Can be used to write any REG_DWORD registry value stored
    under Device Parameter.

Arguments:


Return Value:

   TRUE - if write is successful
   FALSE - otherwise

--*/
{
    WDFKEY          hKey = NULL;
    NTSTATUS        status;
    BOOLEAN         retValue = FALSE;
    UNICODE_STRING  valueName;


    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "Entered PciDrvWriteRegistryValue\n");

    //
    // write the value out to the registry
    //
    status = WdfDeviceOpenRegistryKey(FdoData->WdfDevice,
                                      PLUGPLAY_REGKEY_DEVICE,
                                      STANDARD_RIGHTS_ALL,
                                      WDF_NO_OBJECT_ATTRIBUTES,
                                      &hKey);

    if (NT_SUCCESS (status)) {

        RtlInitUnicodeString(&valueName,Name);

        status = WdfRegistryAssignULong (hKey,
                                         &valueName,
                                         Value );

        if (NT_SUCCESS (status)) {
            retValue = TRUE;
        }

        WdfRegistryClose(hKey);
    }

    return retValue;

}

VOID
PciDrvEvtDriverContextCleanup(
    IN WDFDRIVER Driver
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    Driver - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    PDRIVER_CONTEXT driverContext;

    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                    "--> PciDrvEvtDriverContextCleanup\n");
    PAGED_CODE ();

    driverContext = GetDriverContext(Driver);

    if (driverContext->RecvLookaside) {
        WdfObjectDelete(driverContext->RecvLookaside);
    }
    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP( WdfDriverWdmGetDriverObject( Driver ) );

}



#if !defined(EVENT_TRACING)

VOID
TraceEvents    (
    IN ULONG   TraceEventsLevel,
    IN ULONG   TraceEventsFlag,
    IN PCCHAR  DebugMessage,
    ...
    )

/*++

Routine Description:

    Debug print for the sample driver.

Arguments:

    TraceEventsLevel - print level between 0 and 3, with 3 the most verbose

Return Value:

    None.

 --*/
 {
#if DBG
#define     TEMP_BUFFER_SIZE        512
    va_list    list;
    CHAR       debugMessageBuffer[TEMP_BUFFER_SIZE];
    NTSTATUS   status;

    va_start(list, DebugMessage);

    if (DebugMessage) {

        //
        // Using new safe string functions instead of _vsnprintf.
        // This function takes care of NULL terminating if the message
        // is longer than the buffer.
        //
        status = RtlStringCbVPrintfA( debugMessageBuffer,
                                      sizeof(debugMessageBuffer),
                                      DebugMessage,
                                      list );
        if(!NT_SUCCESS(status)) {

            DbgPrint (_DRIVER_NAME_": RtlStringCbVPrintfA failed %x\n",
                      status);
            return;
        }
        if (TraceEventsLevel <= TRACE_LEVEL_INFORMATION ||
            (TraceEventsLevel <= DebugLevel &&
             ((TraceEventsFlag & DebugFlag) == TraceEventsFlag))) {
            DbgPrint(debugMessageBuffer);
        }
    }
    va_end(list);

    return;
#else
    UNREFERENCED_PARAMETER(TraceEventsLevel);
    UNREFERENCED_PARAMETER(TraceEventsFlag);
    UNREFERENCED_PARAMETER(DebugMessage);
#endif
}

#endif

