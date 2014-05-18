/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    NIC_INIT.c

Abstract:

    Contains rotuines to do resource allocation and hardware
    initialization & shutdown.

Environment:

    Kernel mode

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
#include "nic_init.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, NICAllocateSoftwareResources)
#pragma alloc_text (PAGE, NICFreeSoftwareResources)
#pragma alloc_text (PAGE, NICMapHWResources)
#pragma alloc_text (PAGE, NICUnmapHWResources)
#pragma alloc_text (PAGE, NICGetDeviceInformation)
#pragma alloc_text (PAGE, NICAllocAdapterMemory)
#pragma alloc_text (PAGE, NICFreeAdapterMemory)
#pragma alloc_text (PAGE, NICInitRecvBuffers)
#pragma alloc_text (PAGE, NICAllocRfd)
#pragma alloc_text (PAGE, NICFreeRfd)
#pragma alloc_text (PAGE, NICFreeRfdWorkItem)
#endif


NTSTATUS
NICAllocateSoftwareResources(
    IN OUT PFDO_DATA FdoData
    )
/*++
Routine Description:

    This routine creates two parallel queues and 3 manual queues to hold
    Read, Write and IOCTL requests. It also creates the interrupt object and
    DMA object, and performs some additional initialization.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

     None

--*/
{
    NTSTATUS                        status;
    WDF_IO_QUEUE_CONFIG             ioQueueConfig;

    WDF_DMA_ENABLER_CONFIG          dmaConfig;
    ULONG                           maximumLength, maxLengthSupported;
    WDF_OBJECT_ATTRIBUTES           attributes;
    ULONG                           maxMapRegistersRequired, miniMapRegisters;
    ULONG                           mapRegistersAllocated;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "-->NICAllocateSoftwareResources\n");

    PAGED_CODE();

    //
    // Initialize all the static data first to make sure we don't touch
    // uninitialized list in the ContextCleanup callback if the
    // AddDevice fails for any reason.
    //
    InitializeListHead(&FdoData->RecvList);

    //
    // This a global lock, to synchonize access to device context.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = FdoData->WdfDevice;
    status = WdfSpinLockCreate(&attributes,&FdoData->Lock);
    if(!NT_SUCCESS(status)){
        return status;
    }


    //
    // Get the BUS_INTERFACE_STANDARD for our device so that we can
    // read & write to PCI config space.
    //
    status = WdfFdoQueryForInterface(FdoData->WdfDevice,
                                   &GUID_BUS_INTERFACE_STANDARD,
                                   (PINTERFACE) &FdoData->BusInterface,
                                   sizeof(BUS_INTERFACE_STANDARD),
                                   1, // Version
                                   NULL); //InterfaceSpecificData
    if (!NT_SUCCESS (status)){
        return status;
    }

    //
    // First make sure this is our device before doing whole lot
    // of other things.
    //
    status = NICGetDeviceInformation(FdoData);
    if (!NT_SUCCESS (status)){
        return status;
    }

    NICGetDeviceInfSettings(FdoData);

    //
    // We will create and configure a queue for receiving
    // write requests. If these requests have to be pended for any
    // reason, they will be forwarded to a manual queue created after this one.
    // Framework automatically takes the responsibility of handling
    // cancellation when the requests are waiting in the queue. This is
    // a managed queue. So the framework will take care of queueing
    // incoming requests when the pnp/power state transition takes place.
    // Since we have configured the queue to dispatch all the specific requests
    // we care about, we don't need a default queue. A default queue is
    // used to receive requests that are not preconfigured to go to
    // a specific queue.
    //
    WDF_IO_QUEUE_CONFIG_INIT(
        &ioQueueConfig,
        WdfIoQueueDispatchParallel
    );

    ioQueueConfig.EvtIoWrite = PciDrvEvtIoWrite;

    status = WdfIoQueueCreate(
                 FdoData->WdfDevice,
                 &ioQueueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &FdoData->WriteQueue // queue handle
             );

    if (!NT_SUCCESS (status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

     status = WdfDeviceConfigureRequestDispatching(
                    FdoData->WdfDevice,
                    FdoData->WriteQueue,
                    WdfRequestTypeWrite);

    if(!NT_SUCCESS (status)){
        ASSERT(NT_SUCCESS(status));
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Error in config'ing write Queue 0x%x\n", status);
        return status;
    }

    //
    // Manual internal queue for write reqeusts. This will be used to queue
    // the write requests presented to us from the parallel default queue
    // when we are low in TCB resources or when the device
    // is busy doing link detection.
    // Requests can be canceled while waiting in the queue without any
    // notification to the driver.
    //
    WDF_IO_QUEUE_CONFIG_INIT(
        &ioQueueConfig,
        WdfIoQueueDispatchManual
        );

    status = WdfIoQueueCreate (
                   FdoData->WdfDevice,
                   &ioQueueConfig,
                   WDF_NO_OBJECT_ATTRIBUTES,
                   &FdoData->PendingWriteQueue
                   );

    if(!NT_SUCCESS (status)){
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Error Creating manual write Queue 0x%x\n", status);
        return status;
    }


    //
    // Manual queue for read requests (WdfRequestTypeRead). We will configure the queue
    // so that incoming read requests are directly dispatched to this queue. We will
    // manually remove the requests from the queue and service them in our recv
    // interrupt handler.  WDF_IO_QUEUE_CONFIG_INIT initializes queues to be
    // auto managed by default.
    //
    WDF_IO_QUEUE_CONFIG_INIT(
        &ioQueueConfig,
        WdfIoQueueDispatchManual
        );

    status = WdfIoQueueCreate (
                   FdoData->WdfDevice,
                   &ioQueueConfig,
                   WDF_NO_OBJECT_ATTRIBUTES,
                   &FdoData->PendingReadQueue
                   );

    if(!NT_SUCCESS (status)){
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Error Creating read Queue 0x%x\n", status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
                    FdoData->WdfDevice,
                    FdoData->PendingReadQueue,
                    WdfRequestTypeRead);

    if(!NT_SUCCESS (status)){
        ASSERT(NT_SUCCESS(status));
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Error in config'ing read Queue 0x%x\n", status);
        return status;
    }

    //
    // Alignment requirement must be 16-byte for this device. This alignment
    // value will be inherits by the DMA enabler and used when you allocate
    // common buffers.
    //
    WdfDeviceSetAlignmentRequirement( FdoData->WdfDevice, FILE_OCTA_ALIGNMENT);

    //
    // Bare minimum number of map registers required to do
    // a single NIC_MAX_PACKET_SIZE transfer.
    //
    miniMapRegisters = BYTES_TO_PAGES(NIC_MAX_PACKET_SIZE) + 1;

    //
    // Maximum map registers required to do simultaneous transfer
    // of all TCBs assuming each packet spanning NIC_MAX_PHYS_BUF_COUNT
    // Buffer can span multiple MDLs.
    //
    maxMapRegistersRequired = FdoData->NumTcb * NIC_MAX_PHYS_BUF_COUNT;

    //
    // The maximum length of buffer for maxMapRegistersRequired number of
    // map registers would be.
    //
    maximumLength = (maxMapRegistersRequired-1) << PAGE_SHIFT;

    //
    // Create a new DMA Object for Scatter/Gather DMA mode.
    //
    WDF_DMA_ENABLER_CONFIG_INIT( &dmaConfig,
                                 WdfDmaProfileScatterGather,
                                 maximumLength );

    status = WdfDmaEnablerCreate( FdoData->WdfDevice,
                                  &dmaConfig,
                                  WDF_NO_OBJECT_ATTRIBUTES,
                                  &FdoData->WdfDmaEnabler );

    if (!NT_SUCCESS (status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "WdfDmaEnblerCreate failed: %08X\n", status);
        return status;
    }

    maxLengthSupported = (ULONG) WdfDmaEnablerGetFragmentLength(FdoData->WdfDmaEnabler,
                                                        WdfDmaDirectionReadFromDevice);

    mapRegistersAllocated = BYTES_TO_PAGES(maxLengthSupported) + 1;

    if(mapRegistersAllocated < miniMapRegisters) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "Not enough map registers: Allocated %d, Required %d\n",
                    mapRegistersAllocated, miniMapRegisters);
        status = STATUS_INSUFFICIENT_RESOURCES;
        return status;
    }

    //
    // Adjust our TCB count based on the MapRegisters we got. We will
    // take the best case scenario where the packet is going to span
    // no more than 2 pages.
    //
    FdoData->NumTcb = mapRegistersAllocated/miniMapRegisters;

    //
    // Make sure it doesn't exceed NIC_MAX_TCBS.
    //
    FdoData->NumTcb = min(FdoData->NumTcb, NIC_MAX_TCBS);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "MapRegisters Allocated %d\n", mapRegistersAllocated);
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT,
                "Adjusted TCB count is %d\n", FdoData->NumTcb);

    //
    // Set the maximum allowable DMA Scatter/Gather list fragmentation size.
    //
    WdfDmaEnablerSetMaximumScatterGatherElements( FdoData->WdfDmaEnabler,
                                                  NIC_MAX_PHYS_BUF_COUNT );

    //
    // Create a lock to protect all the write-related buffer lists.
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = FdoData->WdfDevice;
    status = WdfSpinLockCreate(&attributes,&FdoData->SendLock);
    if(!NT_SUCCESS(status)){
        return status;
    }

    //
    // Create a lock to protect all the read-related buffer lists
    //
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = FdoData->WdfDevice;
    status = WdfSpinLockCreate(&attributes,&FdoData->RcvLock);
    if(!NT_SUCCESS(status)){
        return status;
    }

    status = NICAllocAdapterMemory(FdoData);

    if (NT_SUCCESS(status)) {

        //
        // This sets up send buffers.  It doesn't actually touch hardware.
        //

        NICInitSendBuffers(FdoData);

        //
        // This sets up receive buffers.  It doesn't actually touch hardware.
        //

        status = NICInitRecvBuffers(FdoData);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- NICAllocateSoftwareResources\n");

    return status;
}


NTSTATUS
NICFreeSoftwareResources(
    IN OUT PFDO_DATA FdoData
    )
/*++
Routine Description:

    Free all the software resources. We shouldn't touch the hardware.
    This functions is called in the context of EvtDeviceContextCleanup.
    Most of the resources created in NICAllocateResources such as queues,
    DMA enabler, SpinLocks, CommonBuffer, are already freed by
    framework because they are associated with the WDFDEVICE directly
    or indirectly as child objects.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

     None

--*/
{

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "-->NICFreeSoftwareResources\n");

    PAGED_CODE();

    NICFreeAdapterMemory(FdoData);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--NICFreeSoftwareResources\n");

    return STATUS_SUCCESS;

}

NTSTATUS
NICMapHWResources(
    IN OUT PFDO_DATA FdoData,
    WDFCMRESLIST   ResourcesTranslated
    )
/*++
Routine Description:

   Gets the HW resources assigned by the bus driver and maps them
    to system address space. Called during EvtDevicePrepareHardware.

    Three base address registers are supported by the 8255x:
    1) CSR Memory Mapped Base Address Register (BAR 0 at offset 10)
    2) CSR I/O Mapped Base Address Register (BAR 1 at offset 14)
    3) Flash Memory Mapped Base Address Register (BAR 2 at offset 18)

    The 8255x requires one BAR for I/O mapping and one BAR for memory
    mapping of these registers anywhere within the 32-bit memory address space.
    The driver determines which BAR (I/O or Memory) is used to access the
    Control/Status Registers.

    Just for illustration, this driver maps both memory and I/O registers and
    shows how to use READ_PORT_xxx or READ_REGISTER_xxx functions to perform
    I/O in a platform independent basis. On some platforms, the I/O registers
    can get mapped into memory space and your driver should be able to handle
    this transparently.

    One BAR is also required to map the accesses to an optional Flash memory.
    The 82557 implements this register regardless of the presence or absence
    of a Flash chip on the adapter. The 82558 and 82559 implement this
    register only if a bit is set in the EEPROM. The size of the space requested
    by this register is 1Mbyte, and it is always mapped anywhere in the 32-bit
    memory address space.

    Note: Although the 82558 only supports up to 64 Kbytes of Flash memory
    and the 82559 only supports 128 Kbytes of Flash memory, the driver
    requests 1 Mbyte of address space. Software should not access Flash
    addresses above 64 Kbytes for the 82558 or 128 Kbytes for the 82559
    because Flash accesses above the limits are aliased to lower addresses.

Arguments:

    FdoData     Pointer to our FdoData
    ResourcesTranslated     Pointer to list of translated resources passed
                        to EvtDevicePrepareHardware callback

Return Value:


     None

--*/
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR descriptor;
    ULONG       i;
    NTSTATUS    status = STATUS_SUCCESS;
    BOOLEAN     bResPort      = FALSE;
    BOOLEAN     bResInterrupt = FALSE;
    BOOLEAN     bResMemory    = FALSE;
    ULONG       numberOfBARs  = 0;


    PAGED_CODE();

    for (i=0; i<WdfCmResourceListGetCount(ResourcesTranslated); i++) {

        descriptor = WdfCmResourceListGetDescriptor(ResourcesTranslated, i);

        if(!descriptor){
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfResourceCmGetDescriptor");
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        switch (descriptor->Type) {

        case CmResourceTypePort:
            //
            // We will increment the BAR count only for valid resources. We will
            // not count the private device types added by the PCI bus driver.
            //
            numberOfBARs++;

            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "I/O mapped CSR: (%x) Length: (%d)\n",
                descriptor->u.Port.Start.LowPart,
                descriptor->u.Port.Length);

            //
            // The resources are listed in the same order the as
            // BARs in the config space, so this should be the second one.
            //
            if(numberOfBARs != 1) {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "I/O mapped CSR is not in the right order\n");
                status = STATUS_DEVICE_CONFIGURATION_ERROR;
                return status;
            }

            //
            // The port is in I/O space on this machine.
            // We should use READ_PORT_Xxx, and WRITE_PORT_Xxx routines
            // to read or write to the port.
            //

            FdoData->IoBaseAddress = ULongToPtr(descriptor->u.Port.Start.LowPart);
            FdoData->IoRange = descriptor->u.Port.Length;
            //
            // Since all our accesses are USHORT wide, we will create an accessor
            // table just for these two functions.
            //
            FdoData->ReadPort = NICReadPortUShort;
            FdoData->WritePort = NICWritePortUShort;

            bResPort = TRUE;
            FdoData->MappedPorts = FALSE;
            break;

        case CmResourceTypeMemory:

            numberOfBARs++;

            if(numberOfBARs == 2) {
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "Memory mapped CSR:(%x:%x) Length:(%d)\n",
                                        descriptor->u.Memory.Start.LowPart,
                                        descriptor->u.Memory.Start.HighPart,
                                        descriptor->u.Memory.Length);
                //
                // Our CSR memory space should be 0x1000 in size.
                //
                ASSERT(descriptor->u.Memory.Length == 0x1000);
                FdoData->MemPhysAddress = descriptor->u.Memory.Start;
                FdoData->CSRAddress = MmMapIoSpace(
                                                descriptor->u.Memory.Start,
                                                NIC_MAP_IOSPACE_LENGTH,
                                                MmNonCached);
                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "CSRAddress=%p\n", FdoData->CSRAddress);

                bResMemory = TRUE;

            } else if(numberOfBARs == 1){

                TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                    "I/O mapped CSR in Memory Space: (%x) Length: (%d)\n",
                    descriptor->u.Memory.Start.LowPart,
                    descriptor->u.Memory.Length);
                //
                // The port is in memory space on this machine.
                // We should call MmMapIoSpace to map the physical to virtual
                // address, and also use the READ/WRITE_REGISTER_xxx function
                // to read or write to the port.
                //

                FdoData->IoBaseAddress = MmMapIoSpace(
                                                descriptor->u.Memory.Start,
                                                descriptor->u.Memory.Length,
                                                MmNonCached);

                FdoData->ReadPort = NICReadRegisterUShort;
                FdoData->WritePort = NICWriteRegisterUShort;
                FdoData->MappedPorts = TRUE;
                bResPort = TRUE;

            } else {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                            "Memory Resources are not in the right order\n");
                status = STATUS_DEVICE_CONFIGURATION_ERROR;
                return status;
            }

            break;

        case CmResourceTypeInterrupt:

            ASSERT(!bResInterrupt);

            bResInterrupt = TRUE;

            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "Interrupt level: 0x%0x, Vector: 0x%0x\n",
                descriptor->u.Interrupt.Level,
                descriptor->u.Interrupt.Vector);

            break;

        default:
            //
            // This could be device-private type added by the PCI bus driver. We
            // shouldn't filter this or change the information contained in it.
            //
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "Unhandled resource type (0x%x)\n",
                                        descriptor->Type);
            break;
        }

    }

    //
    // Make sure we got all the 3 resources to work with.
    //
    if (!(bResPort && bResInterrupt && bResMemory)) {
        status = STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return status;
}

NTSTATUS
NICUnmapHWResources(
    IN OUT PFDO_DATA FdoData
    )
/*++
Routine Description:

    Disconnect the interrupt and unmap all the memory and I/O resources.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

     None

--*/
{
    PAGED_CODE();

    //
    // Free hardware resources
    //
    if (FdoData->CSRAddress)
    {
        MmUnmapIoSpace(FdoData->CSRAddress, NIC_MAP_IOSPACE_LENGTH);
        FdoData->CSRAddress = NULL;
    }

    if(FdoData->MappedPorts){
        MmUnmapIoSpace(FdoData->IoBaseAddress, FdoData->IoRange);
        FdoData->IoBaseAddress = NULL;
    }

    return STATUS_SUCCESS;

}


NTSTATUS
NICGetDeviceInformation(
    IN PFDO_DATA FdoData
    )
/*++
Routine Description:

    This function reads the PCI config space and make sure that it's our
    device and stores the device IDs and power information in the device
    extension. Should be done in the StartDevice.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

     None

--*/
{
    NTSTATUS            status = STATUS_SUCCESS;
    DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) UCHAR buffer[0xe2];
    PPCI_COMMON_CONFIG  pPciConfig = (PPCI_COMMON_CONFIG) buffer;
    USHORT              usPciCommand;
    ULONG               bytesRead =0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "---> NICGetDeviceInformation\n");

    PAGED_CODE();

    bytesRead = FdoData->BusInterface.GetBusData(
                        FdoData->BusInterface.Context,
                         PCI_WHICHSPACE_CONFIG, //READ
                         buffer,
                         FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID),
                         0xe2);

    if (bytesRead != 0xe2) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                        "GetBusData (NIC_PCI_E100_HDR_LENGTH) failed =%d\n",
                         bytesRead);
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    //
    // Is this our device?
    //

    if (pPciConfig->VendorID != NIC_PCI_VENDOR_ID ||
        pPciConfig->DeviceID != NIC_PCI_DEVICE_ID)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                        "VendorID/DeviceID don't match - %x/%x\n",
                        pPciConfig->VendorID, pPciConfig->DeviceID);
        //return STATUS_DEVICE_DOES_NOT_EXIST;

    }

    //
    // save TRACE_LEVEL_INFORMATION from config space
    //
    FdoData->RevsionID = pPciConfig->RevisionID;
    FdoData->SubVendorID = pPciConfig->u.type0.SubVendorID;
    FdoData->SubSystemID = pPciConfig->u.type0.SubSystemID;

    usPciCommand = pPciConfig->Command;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- NICGetDeviceInformation\n");

    return status;
}

NTSTATUS
NICAllocAdapterMemory(
    IN  PFDO_DATA     FdoData
    )
/*++
Routine Description:

    Allocate all the memory blocks for send, receive and others

Arguments:

    FdoData     Pointer to our adapter

Return Value:


--*/
{
    NTSTATUS        status = STATUS_SUCCESS;
    PUCHAR          pMem;
    ULONG           MemPhys;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> NICAllocAdapterMemory\n");

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "NumTcb=%d\n", FdoData->NumTcb);

    do
    {
        //
        // Send
        //
        //
        // Allocate MP_TCB's
        //
        status = RtlULongMult(FdoData->NumTcb,
                         sizeof(MP_TCB),
                         &FdoData->MpTcbMemSize);
        if(!NT_SUCCESS(status)){
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
                    "RtlUlongMult failed 0x%x\n", status);
            break;
        }

        pMem = ExAllocatePoolWithTag(NonPagedPool,
                            FdoData->MpTcbMemSize, PCIDRV_POOL_TAG);
        if (NULL == pMem )
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "Failed to allocate MP_TCB's\n");
            break;
        }

        RtlZeroMemory(pMem, FdoData->MpTcbMemSize);
        FdoData->MpTcbMem = pMem;

        // HW_START

        //
        // Allocate shared memory for send
        //
        FdoData->HwSendMemAllocSize = FdoData->NumTcb * (sizeof(ULONG)/*TCB*/ +
                                      NIC_MAX_PHYS_BUF_COUNT * sizeof(ULONG/*TBD*/));

        status = WdfCommonBufferCreate( FdoData->WdfDmaEnabler,
                                        FdoData->HwSendMemAllocSize,
                                        WDF_NO_OBJECT_ATTRIBUTES,
                                        &FdoData->WdfSendCommonBuffer );

        if (status != STATUS_SUCCESS)
        {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "WdfCommonBufferCreate(Send) "
                                        "failed %08X\n", status );
            break;
        }

        FdoData->HwSendMemAllocVa = WdfCommonBufferGetAlignedVirtualAddress(
                                               FdoData->WdfSendCommonBuffer);

        FdoData->HwSendMemAllocLa = WdfCommonBufferGetAlignedLogicalAddress(
                                               FdoData->WdfSendCommonBuffer);

        RtlZeroMemory(FdoData->HwSendMemAllocVa,
                      FdoData->HwSendMemAllocSize);

        // HW_END

        //
        // Recv
        //

        // set the max number of RFDs
        // disable the RFD grow/shrink scheme if user specifies a NumRfd value
        // larger than NIC_MAX_GROW_RFDS
        FdoData->MaxNumRfd = max(FdoData->NumRfd, NIC_MAX_GROW_RFDS);

        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "NumRfd = %d\n", FdoData->NumRfd);
        TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "MaxNumRfd = %d\n", FdoData->MaxNumRfd);

        //
        // The driver should allocate more data than sizeof(RFD_STRUC) to allow the
        // driver to align the data(after ethernet header) at 8 byte boundary
        //
        FdoData->HwRfdSize = sizeof(ULONG/*RFD*/);

        status = STATUS_SUCCESS;

    } WHILE( FALSE );

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT,
                "<-- NICAllocAdapterMemory, status=%x\n", status);

    return status;

}

VOID
NICFreeAdapterMemory(
    IN  PFDO_DATA     FdoData
    )
/*++
Routine Description:

    Free all the resources and MP_ADAPTER data block

Arguments:

    FdoData     Pointer to our adapter

Return Value:

    None

--*/
{
    PMP_RFD         pMpRfd;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> NICFreeAdapterMemory\n");

    PAGED_CODE();

    // No active and waiting sends
    ASSERT(FdoData->nBusySend == 0);
    ASSERT(FdoData->nWaitSend == 0);

    ASSERT(FdoData->nReadyRecv == FdoData->CurrNumRfd);

    while (!IsListEmpty(&FdoData->RecvList))
    {
        pMpRfd = (PMP_RFD)RemoveHeadList(&FdoData->RecvList);

        pMpRfd->DeleteCommonBuffer = FALSE;

        NICFreeRfd(FdoData, pMpRfd);
    }

    FdoData->WdfSendCommonBuffer = NULL;
    FdoData->HwSendMemAllocVa = NULL;

    // Free the memory for MP_TCB structures
    if (FdoData->MpTcbMem)
    {
        ExFreePoolWithTag(FdoData->MpTcbMem, PCIDRV_POOL_TAG);
        FdoData->MpTcbMem = NULL;
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- NICFreeAdapterMemory\n");
}



VOID
NICInitSendBuffers(
    IN  PFDO_DATA     FdoData
    )
/*++
Routine Description:

    Initialize send data structures. Can be called at DISPATCH_LEVEL.

Arguments:

    FdoData - Pointer to our adapter context

Return Value:

    None

--*/
{
    PMP_TCB         pMpTcb;
    PULONG         pHwTcb;
    ULONG           HwTcbPhys;
    ULONG           TcbCount;

    PULONG      pHwTbd;
    ULONG           HwTbdPhys;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> NICInitSendBuffers\n");

    // Setup the initial pointers to the SW and HW TCB data space
    pMpTcb = (PMP_TCB) FdoData->MpTcbMem;
    pHwTcb = (PULONG) FdoData->HwSendMemAllocVa;
    HwTcbPhys = FdoData->HwSendMemAllocLa.LowPart;

    // Setup the initial pointers to the TBD data space.
    // TBDs are located immediately following the TCBs
    pHwTbd = (PULONG) (FdoData->HwSendMemAllocVa +
                 (sizeof(ULONG) * FdoData->NumTcb));
    HwTbdPhys = HwTcbPhys + (sizeof(ULONG) * FdoData->NumTcb);

    // Go through and set up each TCB
    for (TcbCount = 0; TcbCount < FdoData->NumTcb; TcbCount++)
    {
        pMpTcb->HwTcb = pHwTcb;                 // save ptr to HW TCB
        pMpTcb->HwTcbPhys = HwTcbPhys;      // save HW TCB physical address
        pMpTcb->HwTbd = pHwTbd;                 // save ptr to TBD array
        pMpTcb->HwTbdPhys = HwTbdPhys;      // save TBD array physical address

        if (TcbCount){
            pMpTcb->PrevHwTcb = pHwTcb - 1;
        }
        else {
            pMpTcb->PrevHwTcb   = (PULONG)((PUCHAR)FdoData->HwSendMemAllocVa +
                                  ((FdoData->NumTcb - 1) * sizeof(ULONG)));
        }

        // Set the link pointer in HW TCB to the next TCB in the chain.
        // If this is the last TCB in the chain, then set it to the first TCB.
        if (TcbCount < FdoData->NumTcb - 1)
        {
            pMpTcb->Next = pMpTcb + 1;
        }
        else
        {
            pMpTcb->Next = (PMP_TCB) FdoData->MpTcbMem;
        }

        pMpTcb++;
        pHwTcb++;
        HwTcbPhys += sizeof(ULONG);
        pHwTbd = (PULONG)((PUCHAR)pHwTbd + sizeof(ULONG) * NIC_MAX_PHYS_BUF_COUNT);
        HwTbdPhys += sizeof(ULONG) * NIC_MAX_PHYS_BUF_COUNT;
    }

    // set the TCB head/tail indexes
    // head is the olded one to free, tail is the next one to use
    FdoData->CurrSendHead = (PMP_TCB) FdoData->MpTcbMem;
    FdoData->CurrSendTail = (PMP_TCB) FdoData->MpTcbMem;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- NICInitSendBuffers\n");
}

NTSTATUS
NICInitRecvBuffers(
    IN  PFDO_DATA     FdoData
    )
/*++
Routine Description:

    Initialize receive data structures

Arguments:

    FdoData - Pointer to our adapter context

Return Value:

--*/
{
    NTSTATUS        status = STATUS_INSUFFICIENT_RESOURCES;
    PMP_RFD         pMpRfd;
    ULONG           RfdCount;
    WDFMEMORY       memoryHdl;
    PDRIVER_CONTEXT  driverContext = GetDriverContext(WdfGetDriver());

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> NICInitRecvBuffers\n");

    PAGED_CODE();

    // Setup each RFD
    for (RfdCount = 0; RfdCount < FdoData->NumRfd; RfdCount++)
    {
        status = WdfMemoryCreateFromLookaside(
                driverContext->RecvLookaside,
                &memoryHdl
                );
        if(!NT_SUCCESS(status)){
            TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "Failed to get lookaside buffer\n");
            continue;
        }
        pMpRfd = WdfMemoryGetBuffer(memoryHdl, NULL);
        if (!pMpRfd)
        {
            //ErrorValue = ERRLOG_OUT_OF_LOOKASIDE_MEMORY;
            continue;
        }
        pMpRfd->LookasideMemoryHdl = memoryHdl;
        //
        // Allocate the shared memory for this RFD.
        //
        status = WdfCommonBufferCreate( FdoData->WdfDmaEnabler,
                                        FdoData->HwRfdSize,
                                        WDF_NO_OBJECT_ATTRIBUTES,
                                        &pMpRfd->WdfCommonBuffer );

        if (status != STATUS_SUCCESS)
        {
            pMpRfd->WdfCommonBuffer = NULL;
            WdfObjectDelete(pMpRfd->LookasideMemoryHdl);
            continue;
        }

        pMpRfd->OriginalHwRfd =
            WdfCommonBufferGetAlignedVirtualAddress(pMpRfd->WdfCommonBuffer);

        pMpRfd->OriginalHwRfdLa =
            WdfCommonBufferGetAlignedLogicalAddress(pMpRfd->WdfCommonBuffer);

        //
        // Get a 8-byts aligned memory from the original HwRfd
        //
        pMpRfd->HwRfd = (PULONG)DATA_ALIGN(pMpRfd->OriginalHwRfd);

        //
        // Update physical address accordingly
        //
        pMpRfd->HwRfdLa.QuadPart = pMpRfd->OriginalHwRfdLa.QuadPart +
                         BYTES_SHIFT(pMpRfd->HwRfd, pMpRfd->OriginalHwRfd);

        status = NICAllocRfd(FdoData, pMpRfd);
        if (!NT_SUCCESS(status))
        {
            WdfObjectDelete(pMpRfd->LookasideMemoryHdl);
            continue;
        }
        //
        // Add this RFD to the RecvList
        //
        FdoData->CurrNumRfd++;
        NICReturnRFD(FdoData, pMpRfd);
    }

    if (FdoData->CurrNumRfd > NIC_MIN_RFDS)
    {
        status = STATUS_SUCCESS;
    }

    //
    // FdoData->CurrNumRfd < NIC_MIN_RFDs
    //
    if (status != STATUS_SUCCESS)
    {
        // TODO: Log an entry into the eventlog
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- NICInitRecvBuffers, status=%x\n", status);

    return status;
}

NTSTATUS
NICAllocRfd(
    IN  PFDO_DATA     FdoData,
    IN  PMP_RFD       pMpRfd
    )
/*++
Routine Description:

    Allocate NDIS_PACKET and NDIS_BUFFER associated with a RFD.
    Can be called at DISPATCH_LEVEL.

Arguments:

    FdoData     Pointer to our adapter
    pMpRfd      pointer to a RFD

Return Value:


--*/
{
    NTSTATUS    status = STATUS_SUCCESS;
    PULONG     pHwRfd;

    UNREFERENCED_PARAMETER(FdoData);

    PAGED_CODE();

    do{
        pHwRfd = pMpRfd->HwRfd;
        pMpRfd->HwRfdPhys = pMpRfd->HwRfdLa.LowPart;

        pMpRfd->Flags = 0;

        pMpRfd->Mdl = IoAllocateMdl((PVOID)pHwRfd,
                                    4,
                                    FALSE,
                                    FALSE,
                                    NULL);
        if (!pMpRfd->Mdl)
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        MmBuildMdlForNonPagedPool(pMpRfd->Mdl);

        pMpRfd->Buffer = pHwRfd;
    } WHILE (FALSE);


    if (!NT_SUCCESS(status))
    {
        if (pMpRfd->WdfCommonBuffer)
        {
            //
            // Free HwRfd, we need to free the original memory
            // pointed by OriginalHwRfd.
            //
            WdfObjectDelete( pMpRfd->WdfCommonBuffer );

            pMpRfd->WdfCommonBuffer  = NULL;
            pMpRfd->HwRfd            = NULL;
            pMpRfd->OriginalHwRfd    = NULL;

            pMpRfd->DeleteCommonBuffer = TRUE;
        }
    }

    return status;

}

VOID
NICFreeRfd(
    IN  PFDO_DATA     FdoData,
    IN  PMP_RFD       pMpRfd
    )
/*++
Routine Description:

    Free a RFD.
    Can be called at DISPATCH_LEVEL.

Arguments:

    FdoData     Pointer to our adapter
    pMpRfd      Pointer to a RFD

Return Value:

    None

--*/
{
    UNREFERENCED_PARAMETER(FdoData);
    PAGED_CODE();

    ASSERT(pMpRfd->HwRfd);
    ASSERT(pMpRfd->Mdl);

    IoFreeMdl(pMpRfd->Mdl);

    //
    // Free HwRfd, we need to free the original memory pointed
    // by OriginalHwRfd.
    //
    if (pMpRfd->DeleteCommonBuffer == TRUE) {

        WdfObjectDelete( pMpRfd->WdfCommonBuffer );
    }

    pMpRfd->WdfCommonBuffer = NULL;
    pMpRfd->HwRfd = NULL;
    pMpRfd->OriginalHwRfd = NULL;

    WdfObjectDelete(pMpRfd->LookasideMemoryHdl);
}


VOID
NICShutdown(
    IN  PFDO_DATA     FdoData)
/*++

Routine Description:

    Shutdown the device

Arguments:

    FdoData -  Pointer to our adapter

Return Value:

    None

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "---> NICShutdown\n");

    if(FdoData->CSRAddress) {

    }
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--- NICShutdown\n");
}

VOID
HwSoftwareReset(
    IN  PFDO_DATA     FdoData
    )
/*++
Routine Description:

    Issue a software reset to the hardware

Arguments:

    FdoData     Pointer to our adapter

Return Value:

    None

--*/
{
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> HwSoftwareReset\n");

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "<-- HwSoftwareReset\n");
}

VOID
NICGetDeviceInfSettings(
    IN OUT  PFDO_DATA   FdoData
    )
{

    //
    // Number of ReceiveFrameDescriptors
    //
    if(!PciDrvReadRegistryValue(FdoData,
                                L"NumRfd",
                                &FdoData->NumRfd)){
        FdoData->NumRfd = 16;
    }

    FdoData->NumRfd = min(FdoData->NumRfd, NIC_MAX_RFDS);
    FdoData->NumRfd = max(FdoData->NumRfd, 1);

    //
    // Number of Transmit Control Blocks
    //
    if(!PciDrvReadRegistryValue(FdoData,
                                L"NumTcb",
                                &FdoData->NumTcb)){
        FdoData->NumTcb  = NIC_DEF_TCBS;

    }

    FdoData->NumTcb = min(FdoData->NumTcb, NIC_MAX_TCBS);
    FdoData->NumTcb = max(FdoData->NumTcb, 1);

    return;
 }


