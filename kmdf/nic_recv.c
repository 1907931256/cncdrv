/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:
    NIC_RECV.C

Abstract:
    This module contains miniport receive routines

Environment:

    Kernel mode

--*/

#include "precomp.h"

#if defined(EVENT_TRACING)
#include "nic_recv.tmh"
#endif


__drv_sameIRQL
__drv_requiresIRQL(DISPATCH_LEVEL)
VOID
NICHandleRecvInterrupt(
    IN  PFDO_DATA  FdoData
    )
/*++
Routine Description:

    Interrupt handler for receive processing. Put the received packets
    into an array and call NICServiceReadIrps. If we run low on
    RFDs, allocate another one.

    Assumption: This function is called with the Rcv SPINLOCK held.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

    None

--*/
{
    PMP_RFD         pMpRfd = NULL;
    PULONG          pHwRfd = NULL;

    PMP_RFD         PacketArray[NIC_DEF_RFDS];
    PMP_RFD         PacketFreeArray[NIC_DEF_RFDS];
    UINT            PacketArrayCount;
    UINT            PacketFreeCount;
    UINT            Index;
    UINT            LoopIndex = 0;
    UINT            LoopCount = NIC_MAX_RFDS / NIC_DEF_RFDS + 1;    // avoid staying here too long

    BOOLEAN         bContinue = TRUE;
    BOOLEAN         bAllocNewRfd = FALSE;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "---> NICHandleRecvInterrupt\n");

    ASSERT(FdoData->nReadyRecv >= NIC_MIN_RFDS);

    while (LoopIndex++ < LoopCount && bContinue)
    {
        PacketArrayCount = 0;
        PacketFreeCount = 0;

        //
        // Process up to the array size RFD's
        //
        while (PacketArrayCount < NIC_DEF_RFDS)
        {
            if (IsListEmpty(&FdoData->RecvList))
            {
                ASSERT(FdoData->nReadyRecv == 0);
                bContinue = FALSE;
                break;
            }

            //
            // Get the next MP_RFD to process
            //
            pMpRfd = (PMP_RFD)GetListHeadEntry(&FdoData->RecvList);

            //
            // Get the associated HW_RFD
            //
            pHwRfd = pMpRfd->HwRfd;

            //
            // Remove the RFD from the head of the List
            //
            RemoveEntryList((PLIST_ENTRY)pMpRfd);
            FdoData->nReadyRecv--;

            ASSERT(MP_TEST_FLAG(pMpRfd, fMP_RFD_RECV_READY));
            MP_CLEAR_FLAG(pMpRfd, fMP_RFD_RECV_READY);

            pMpRfd->PacketSize = 4;

            KeFlushIoBuffers(pMpRfd->Mdl, TRUE, TRUE);

            MP_SET_FLAG(pMpRfd, fMP_RFD_RECV_PEND);

            PacketArray[PacketArrayCount] = pMpRfd;
            PacketArrayCount++;
        }

        //
        // if we didn't process any receives, just return from here
        //
        if (PacketArrayCount == 0)
        {
            break;
        }


        WdfSpinLockRelease(FdoData->RcvLock);

        NICServiceReadIrps(
            FdoData,
            PacketArray,
            PacketArrayCount);


        WdfSpinLockAcquire(FdoData->RcvLock);

        //
        // Return all the RFDs to the pool.
        //
        for (Index = 0; Index < PacketFreeCount; Index++)
        {

            //
            // Get the MP_RFD saved in this packet, in NICAllocRfd
            //
            pMpRfd = PacketFreeArray[Index];

            ASSERT(MP_TEST_FLAG(pMpRfd, fMP_RFD_RESOURCES));
            MP_CLEAR_FLAG(pMpRfd, fMP_RFD_RESOURCES);

            NICReturnRFD(FdoData, pMpRfd);
        }

    }

    ASSERT(FdoData->nReadyRecv >= NIC_MIN_RFDS);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<--- NICHandleRecvInterrupt\n");
}

VOID
NICReturnRFD(
    IN  PFDO_DATA FdoData,
    IN  PMP_RFD     pMpRfd
    )
/*++
Routine Description:

    Recycle a RFD and put it back onto the receive list

    Assumption: This function is called with the Rcv SPINLOCK held.

Arguments:

    FdoData     Pointer to our FdoData
    pMpRfd      Pointer to the RFD

Return Value:

    None

--*/
{
    PMP_RFD   pLastMpRfd;
    PULONG   pHwRfd = pMpRfd->HwRfd;

    ASSERT(pMpRfd->Flags == 0);
    MP_SET_FLAG(pMpRfd, fMP_RFD_RECV_READY);

    //
    // The processing on this RFD is done, so put it back on the tail of
    // our list
    //
    InsertTailList(&FdoData->RecvList, (PLIST_ENTRY)pMpRfd);
    FdoData->nReadyRecv++;
    ASSERT(FdoData->nReadyRecv <= FdoData->CurrNumRfd);
}

NTSTATUS
NICStartRecv(
    IN  PFDO_DATA  FdoData
    )
/*++
Routine Description:

    Start the receive unit if it's not in a ready state

    Assumption: This function is called with the Rcv SPINLOCK held.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

    NT Status code

--*/
{
    PMP_RFD         pMpRfd;
    NTSTATUS        status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "---> NICStartRecv\n");
    ASSERT(!IsListEmpty(&FdoData->RecvList));

    //
    // Get the MP_RFD head
    //
    pMpRfd = (PMP_RFD)GetListHeadEntry(&FdoData->RecvList);

    NICHandleRecvInterrupt(FdoData);
    ASSERT(!IsListEmpty(&FdoData->RecvList));

    //
    // Get the new MP_RFD head
    //
    pMpRfd = (PMP_RFD)GetListHeadEntry(&FdoData->RecvList);

exit:
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<--- NICStartRecv, Status=%x\n", status);
    return status;
}


VOID
NICServiceReadIrps(
    PFDO_DATA   FdoData,
    PMP_RFD     *PacketArray,
    ULONG       PacketArrayCount
    )
/*++
Routine Description:

    Copy the data from the recv buffers to pending read IRP buffers
    and complete the IRP. When used as network driver, copy operation
    can be avoided by devising a private interface between us and the
    NDIS-WDM filter and have the NDIS-WDM edge to indicate our buffers
    directly to NDIS.

    Called at DISPATCH_LEVEL. Take advantage of that fact while
     acquiring spinlocks.

Arguments:

    FdoData     Pointer to our FdoData

Return Value:

     None

--*/
{
    PMP_RFD             pMpRfd = NULL;
    ULONG               index;
    NTSTATUS            status;
    PVOID               buffer;
    WDFREQUEST          request;
    size_t              bufLength=0;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "--> NICServiceReadIrps\n");


    for(index=0; index < PacketArrayCount; index++)
    {
        pMpRfd = PacketArray[index];
        ASSERT(pMpRfd);

        do {
            status = WdfIoQueueRetrieveNextRequest( FdoData->PendingReadQueue,
                                                    &request );

            if(NT_SUCCESS(status)){

                WDF_REQUEST_PARAMETERS  params;
                ULONG                   length = 0;

                WDF_REQUEST_PARAMETERS_INIT(&params);

                WdfRequestGetParameters(
                    request,
                    &params
                     );

                ASSERT(status == STATUS_SUCCESS);

                bufLength = params.Parameters.Read.Length;

                status = WdfRequestRetrieveOutputBuffer(request,
                                                        bufLength,
                                                        &buffer,
                                                        &bufLength);
                if(NT_SUCCESS(status) ) {

                    length = min((ULONG)bufLength, pMpRfd->PacketSize);

                    RtlCopyMemory(buffer, pMpRfd->Buffer, length);

                    Hexdump((TRACE_LEVEL_VERBOSE, DBG_READ,
                             "Received Packet Data: %!HEXDUMP!\n",
                             log_xstr(buffer, (USHORT)length)));
                    FdoData->BytesReceived += length;
                }

                WdfRequestCompleteWithInformation(request, status, length);
                break;
            }else {
                ASSERTMSG("WdfIoQueueRetrieveNextRequest failed",
                          (status == STATUS_NO_MORE_ENTRIES ||
                           status == STATUS_WDF_PAUSED));
                break;
            }

        } WHILE (TRUE);


        WdfSpinLockAcquire(FdoData->RcvLock);

        ASSERT(MP_TEST_FLAG(pMpRfd, fMP_RFD_RECV_PEND));
        MP_CLEAR_FLAG(pMpRfd, fMP_RFD_RECV_PEND);


        NICReturnRFD(FdoData, pMpRfd);

        WdfSpinLockRelease(FdoData->RcvLock);

    }// end of loop

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_READ, "<-- NICServiceReadIrps\n");

    return;

}




