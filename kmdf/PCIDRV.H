/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    PciDrv.h

Abstract:

    Header file for the PCIDRV driver modules.

Environment:

    Kernel mode

--*/


#if !defined(_PCIDRV_H_)
#define _PCIDRV_H_

//
// Let us use newly introduced (.NET DDK) safe string function to avoid
// security issues related buffer overrun.
// The advantages of the RtlStrsafe functions include:
// 1) The size of the destination buffer is always provided to the
// function to ensure that the function does not write past the end of
// the buffer.
// 2) Buffers are guaranteed to be null-terminated, even if the
// operation truncates the intended result.
//

//
// In this driver we are using a safe version vsnprintf, which is
// RtlStringCbVPrintfA. To use strsafe function on 9x, ME, and Win2K Oses, we
// have to define NTSTRSAFE_LIB before including this header file and explicitly
// link to ntstrsafe.lib. If your driver is just target for XP and above, there is
// no define NTSTRSAFE_LIB and link to the lib.
//
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

//-----------------------------------------------------------------------------
// 4127 -- Conditional Expression is Constant warning
//-----------------------------------------------------------------------------
#define WHILE(constant) \
__pragma(warning(suppress: 4127)) while(constant)

#define _DRIVER_NAME_ "PCIDRV"

#define PCIDRV_POOL_TAG (ULONG) 'DICP'
#define PCIDRV_FDO_INSTANCE_SIGNATURE (ULONG) 'odFT'

#define MILLISECONDS_TO_100NS   (10000)
#define SECOND_TO_MILLISEC      (1000)
#define SECOND_TO_100NS         (SECOND_TO_MILLISEC * MILLISECONDS_TO_100NS)

//
// Bit Flag Macros
//

#define SET_FLAG(Flags, Bit)    ((Flags) |= (Bit))
#define CLEAR_FLAG(Flags, Bit)  ((Flags) &= ~(Bit))
#define TEST_FLAG(Flags, Bit)   (((Flags) & (Bit)) != 0)

//
// The driver context contains global data to the whole driver.
//
typedef struct _DRIVER_CONTEXT {
    //
    // The assumption here is that there is nothing device specific in the lookaside list
    // and hence the same list can be used to do allocations for multiple devices.
    //
    WDFLOOKASIDE            RecvLookaside;

} DRIVER_CONTEXT, * PDRIVER_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, GetDriverContext)

//
// The device extension for the device object
//
typedef struct _FDO_DATA
{
    ULONG                   Signature;       // must be PCIDRV_FDO_INSTANCE_SIGNATURE
                                             // beneath this device object.
    WDFDEVICE               WdfDevice;
    // Following fields are specific to the hardware
    // Configuration
    ULONG                   Flags;
    // IDs
    UCHAR                   RevsionID;
    USHORT                  SubVendorID;
    USHORT                  SubSystemID;

    // HW Resources
    PULONG                  IoBaseAddress;
    ULONG                   IoRange;
    PHYSICAL_ADDRESS        MemPhysAddress;

    BOOLEAN                 MappedPorts;
    PULONG                  CSRAddress;
	BUS_INTERFACE_STANDARD  BusInterface;
    PREAD_PORT              ReadPort;
    PWRITE_PORT             WritePort;
    WDFDMAENABLER           WdfDmaEnabler;

    // SEND
    PMP_TCB                 CurrSendHead;
    PMP_TCB                 CurrSendTail;
    ULONG                   nBusySend;
    LONG                    nWaitSend;
    LONG                    nCancelSend;
    WDFQUEUE                WriteQueue;
    WDFQUEUE                PendingWriteQueue;
    SINGLE_LIST_ENTRY       SendBufList;
    WDFSPINLOCK             SendLock;

    ULONG                   NumTcb;             // Total number of TCBs
    LONG                    RegNumTcb;          // 'NumTcb'


    __field_ecount(MpTcbMemSize) PUCHAR MpTcbMem;
    ULONG                   MpTcbMemSize;

    WDFCOMMONBUFFER         WdfSendCommonBuffer;

    __field_ecount(HwSendMemAllocSize) PUCHAR HwSendMemAllocVa;
    ULONG                   HwSendMemAllocSize;
    PHYSICAL_ADDRESS        HwSendMemAllocLa;     // Logical Address

    // RECV
    LIST_ENTRY              RecvList;
    ULONG                   nReadyRecv;
    LONG                    RefCount;

    ULONG                   NumRfd;
    ULONG                   CurrNumRfd;
    ULONG                   MaxNumRfd;
    ULONG                   HwRfdSize;

    WDFQUEUE                PendingReadQueue;
    WDFSPINLOCK             RcvLock;

    BOOLEAN                 AllocNewRfd;

    // spin locks for protecting misc variables
    WDFSPINLOCK         Lock;
    ULONG                   HwErrCount;
    // Count of bytes received & transmitted
    ULONG64                 BytesReceived;
    ULONG64                 BytesTransmitted;
}  FDO_DATA, *PFDO_DATA;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(FDO_DATA, FdoGetData)

#define CLRMASK(x, mask)     ((x) &= ~(mask));
#define SETMASK(x, mask)     ((x) |=  (mask));


//
// Function prototypes
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD PciDrvEvtDeviceAdd;

EVT_WDF_OBJECT_CONTEXT_CLEANUP PciDrvEvtDriverContextCleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP PciDrvEvtDeviceContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE PciDrvEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE PciDrvEvtDeviceReleaseHardware;

NTSTATUS
PciDrvReturnResources (
    IN OUT PFDO_DATA FdoData
    );

BOOLEAN
PciDrvReadRegistryValue(
    __in  PFDO_DATA  FdoData,
    __in  PWCHAR     Name,
    __out PULONG     Value
    );

BOOLEAN
PciDrvWriteRegistryValue(
    __in PFDO_DATA  FdoData,
    __in PWCHAR     Name,
    __in ULONG      Value
    );

#if defined(WIN2K)

NTKERNELAPI
VOID
ExFreePoolWithTag(
    __in PVOID P,
    __in ULONG Tag
    );

#endif

#endif  // _PCIDRV_H_


