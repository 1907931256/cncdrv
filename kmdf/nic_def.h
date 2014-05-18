/****************************************************************************
** COPYRIGHT (C) 1994-1997 INTEL CORPORATION                               **
** DEVELOPED FOR MICROSOFT BY INTEL CORP., HILLSBORO, OREGON               **
** HTTP://WWW.INTEL.COM/                                                   **
** THIS FILE IS PART OF THE INTEL ETHEREXPRESS PRO/100B(TM) AND            **
** ETHEREXPRESS PRO/100+(TM) NDIS 5.0 MINIPORT SAMPLE DRIVER               **
****************************************************************************/

#ifndef _NIC_DEF_H
#define _NIC_DEF_H

// MP_RFD flags
#define fMP_RFD_RECV_PEND                      0x00000001
#define fMP_RFD_ALLOC_PEND                     0x00000002
#define fMP_RFD_RECV_READY                     0x00000004
#define fMP_RFD_RESOURCES                      0x00000008

// MP_TCB flags
#define fMP_TCB_IN_USE                         0x00000001
#define fMP_TCB_USE_LOCAL_BUF                  0x00000002


// packet and header sizes
#define NIC_MAX_PACKET_SIZE             4
#define NIC_MIN_PACKET_SIZE             4

// NIC PCI Device and vendor IDs
#define NIC_PCI_DEVICE_ID               0x0300
#define NIC_PCI_VENDOR_ID               0x10ee

// IO space length
#define NIC_MAP_IOSPACE_LENGTH          16

// change to your company name instead of using Microsoft
#define NIC_VENDOR_DESC                 "vkorehov"

// number of TCBs per processor - min, default and max
#define NIC_MIN_TCBS                    16
#define NIC_DEF_TCBS                    16
#define NIC_MAX_TCBS                    16




// max number of physical fragments supported per TCB
#define NIC_MAX_PHYS_BUF_COUNT          8

// number of RFDs - min, default and max
#define MIN_NUM_RFD                     16
#define NIC_MIN_RFDS                    16
#define NIC_DEF_RFDS                    16
#define NIC_MAX_RFDS                    16

// only grow the RFDs up to this number
#define NIC_MAX_GROW_RFDS               16

// How many intervals before the RFD list is shrinked?
#define NIC_RFD_SHRINK_THRESHOLD        0

// local data buffer size (to copy send packet data into a local buffer)
#define NIC_BUFFER_SIZE                 4

// max number of send packets the MiniportSendPackets function can accept
#define NIC_MAX_SEND_PACKETS            10

#define ListNext(_pL)                       (_pL)->Flink

#define ListPrev(_pL)                       (_pL)->Blink

#define ALIGN_16                   16

//
// The driver should put the data(after Ethernet header) at 8-bytes boundary
//
#define ETH_DATA_ALIGN                      8   // the data(after Ethernet header) should be 8-byte aligned
//

//
// The driver has to allocate more data then HW_RFD needs to allow shifting data
//
#define MORE_DATA_FOR_ALIGN         (ETH_DATA_ALIGN + HWRFD_SHIFT_OFFSET)
//
// Get a 8-bytes aligned memory address from a given the memory address.
// If the given address is not 8-bytes aligned, return  the closest bigger memory address
// which is 8-bytes aligned.
//
#define DATA_ALIGN(_Va)             ((PVOID)(((ULONG_PTR)(_Va) + (ETH_DATA_ALIGN - 1)) & ~(ETH_DATA_ALIGN - 1)))
//
// Get the number of bytes the final address shift from the original address
//
#define BYTES_SHIFT(_NewVa, _OrigVa) ((PUCHAR)(_NewVa) - (PUCHAR)(_OrigVa))

//--------------------------------------
// Some utility macros
//--------------------------------------
#ifndef min
#define min(_a, _b)     (((_a) < (_b)) ? (_a) : (_b))
#endif

#ifndef max
#define max(_a, _b)     (((_a) > (_b)) ? (_a) : (_b))
#endif

#define MP_ALIGNMEM(_p, _align) (((_align) == 0) ? (_p) : (PUCHAR)(((ULONG_PTR)(_p) + ((_align)-1)) & (~((ULONG_PTR)(_align)-1))))
#define MP_ALIGNMEM_PHYS(_p, _align) (((_align) == 0) ?  (_p) : (((ULONG)(_p) + ((_align)-1)) & (~((ULONG)(_align)-1))))
#define MP_ALIGNMEM_PA(_p, _align) (((_align) == 0) ?  (_p).QuadPart : (((_p).QuadPart + ((_align)-1)) & (~((ULONGLONG)(_align)-1))))

#define GetListHeadEntry(ListHead)  ((ListHead)->Flink)
#define GetListTailEntry(ListHead)  ((ListHead)->Blink)
#define GetListFLink(ListEntry)     ((ListEntry)->Flink)

#define IsSListEmpty(ListHead)  (((PSINGLE_LIST_ENTRY)ListHead)->Next == NULL)

//--------------------------------------
// Macros for flag and ref count operations
//--------------------------------------
#define MP_SET_FLAG(_M, _F)         ((_M)->Flags |= (_F))
#define MP_CLEAR_FLAG(_M, _F)       ((_M)->Flags &= ~(_F))
#define MP_CLEAR_FLAGS(_M)          ((_M)->Flags = 0)
#define MP_TEST_FLAG(_M, _F)        (((_M)->Flags & (_F)) != 0)
#define MP_TEST_FLAGS(_M, _F)       (((_M)->Flags & (_F)) == (_F))

//--------------------------------------
// TCB (Transmit Control Block)
//--------------------------------------
typedef struct _MP_TCB
{
    struct _MP_TCB    *Next;
    ULONG             Flags;
    ULONG             Count;
    WDFDMATRANSACTION DmaTransaction;

    PULONG           HwTcb;            // ptr to HW TCB VA
    ULONG            HwTcbPhys;        // ptr to HW TCB PA
    PULONG           PrevHwTcb;        // ptr to previous HW TCB VA

    PULONG           HwTbd;            // ptr to first TBD
    ULONG            HwTbdPhys;        // ptr to first TBD PA

} MP_TCB, *PMP_TCB;

//--------------------------------------
// RFD (Receive Frame Descriptor)
//--------------------------------------
typedef struct _MP_RFD
{
    LIST_ENTRY              List;
    PVOID                   Buffer;           // Pointer to Buffer
    PMDL                    Mdl;
    PULONG                  HwRfd;            // ptr to hardware RFD
    WDFCOMMONBUFFER         WdfCommonBuffer;
    PULONG                  OriginalHwRfd;    // ptr to shared memory
    PHYSICAL_ADDRESS        HwRfdLa;          // logical address of RFD
    PHYSICAL_ADDRESS        OriginalHwRfdLa;  // Original physical address allocated by NDIS
    ULONG                   HwRfdPhys;        // lower part of HwRfdPa
    BOOLEAN                 DeleteCommonBuffer; // Indicates if WdfObjectDelete
                                                      // is to be called when freeing MD_RFD.
    ULONG                   Flags;
    ULONG                   PacketSize;       // total size of receive frame
    WDFMEMORY               LookasideMemoryHdl;
} MP_RFD, *PMP_RFD;


//--------------------------------------
// Macros specific to miniport adapter structure
//--------------------------------------
#define MP_TCB_RESOURCES_AVAIABLE(_M) ((_M)->nBusySend < (_M)->NumTcb)

#define MP_OFFSET(field)   ((UINT)FIELD_OFFSET(MP_ADAPTER,field))
#define MP_SIZE(field)     sizeof(((PMP_ADAPTER)0)->field)


//--------------------------------------
// Stall execution and wait with timeout
//--------------------------------------
/*++
    _condition  - condition to wait for
    _timeout_ms - timeout value in milliseconds
    _result     - TRUE if condition becomes true before it times out
--*/
#define MP_STALL_AND_WAIT(_condition, _timeout_ms, _result)     \
{                                                               \
    int counter;                                                \
    _result = FALSE;                                            \
    for(counter = _timeout_ms * 50; counter != 0; counter--)    \
    {                                                           \
        if(_condition)                                          \
        {                                                       \
            _result = TRUE;                                     \
            break;                                              \
        }                                                       \
        KeStallExecutionProcessor(20);                          \
    }                                                           \
}

__inline VOID MP_STALL_EXECUTION(
   IN ULONG MsecDelay)
{
    // Delay in 100 usec increments
    MsecDelay *= 10;
    while (MsecDelay)
    {
        KeStallExecutionProcessor(100);
        MsecDelay--;
    }
}

typedef struct _FDO_DATA FDO_DATA, *PFDO_DATA;

NTSTATUS
NICGetDeviceInformation(
    IN OUT PFDO_DATA FdoData
    );


NTSTATUS
NICAllocateSoftwareResources(
    IN OUT PFDO_DATA FdoData
    );

NTSTATUS
NICMapHWResources(
    IN OUT PFDO_DATA FdoData,
    WDFCMRESLIST   ResourcesTranslated
    );

NTSTATUS
NICUnmapHWResources(
    IN OUT PFDO_DATA FdoData
    );


NTSTATUS
NICFreeSoftwareResources(
    IN OUT PFDO_DATA FdoData
    );

NTSTATUS
NICInitializeAdapter(
    IN  PFDO_DATA     FdoData
    );



VOID
HwSoftwareReset(
    IN  PFDO_DATA     FdoData
    );

EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED NICEvtDeviceD0EntryPostInterruptsEnabled;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED NICEvtDeviceD0ExitPreInterruptsDisabled;

EVT_WDF_IO_QUEUE_IO_WRITE PciDrvEvtIoWrite;

EVT_WDF_PROGRAM_DMA NICEvtProgramDmaFunction;

EVT_WDF_TIMER NICWatchDogEvtTimerFunc;

EVT_WDF_WORKITEM NICAllocRfdWorkItem;
EVT_WDF_WORKITEM NICFreeRfdWorkItem;

NTSTATUS
NICAllocAdapterMemory(
    IN  PFDO_DATA     FdoData
    );

VOID
NICFreeAdapterMemory(
    IN  PFDO_DATA     FdoData
    );

NTSTATUS
NICAllocRfd(
    IN  PFDO_DATA     FdoData,
    IN  PMP_RFD         pMpRfd
    );

VOID
NICFreeRfd(
    IN  PFDO_DATA     FdoData,
    IN  PMP_RFD         pMpRfd
    );

VOID
NICReturnRFD(
    IN  PFDO_DATA FdoData,
    IN  PMP_RFD     pMpRfd
    );

NTSTATUS
HwConfigure(
    IN  PFDO_DATA     FdoData
    );

NTSTATUS
HwSetupIAAddress(
    IN  PFDO_DATA     FdoData
    );


NTSTATUS
NICInitRecvBuffers(
    IN  PFDO_DATA     FdoData
    );

VOID
NICInitSendBuffers(
    IN  PFDO_DATA     FdoData
    );


VOID
NICServiceIndicateStatusIrp(
    IN PFDO_DATA        FdoData
    );


NTSTATUS
NICWritePacket(
    IN  PFDO_DATA               FdoData,
    IN  WDFDMATRANSACTION       DmaTransaction,
    IN  PSCATTER_GATHER_LIST    SGList
    );

NTSTATUS
NICSendPacket(
    IN  PFDO_DATA     FdoData,
    IN  PMP_TCB       pMpTcb,
    IN  PSCATTER_GATHER_LIST   ScatterGather);

NTSTATUS
NICStartSend(
    IN  PFDO_DATA     FdoData,
    IN  PMP_TCB       pMpTcb);

NTSTATUS
NICHandleSendInterrupt(
    IN  PFDO_DATA  FdoData
    );

VOID
NICCheckForQueuedSends(
    IN  PFDO_DATA  FdoData
    );

__drv_sameIRQL
__drv_requiresIRQL(DISPATCH_LEVEL)
VOID
NICFreeQueuedSendPackets(
    IN  PFDO_DATA     FdoData
    );

VOID
NICFreeBusySendPackets(
    IN  PFDO_DATA  FdoData
    );

VOID
NICCompleteSendRequest(
    PFDO_DATA FdoData,
    WDFREQUEST Request,
    NTSTATUS Status,
    ULONG   Information
    );

VOID
NICShutdown(
    IN  PFDO_DATA  FdoData
    );

__drv_sameIRQL
__drv_requiresIRQL(DISPATCH_LEVEL)
VOID
NICHandleRecvInterrupt(
    IN  PFDO_DATA  FdoData
    );

NTSTATUS
NICStartRecv(
    IN  PFDO_DATA  FdoData
    );

VOID
NICResetRecv(
    IN  PFDO_DATA   FdoData
    );

VOID
NICServiceReadIrps(
    PFDO_DATA FdoData,
    PMP_RFD *PacketArray,
    ULONG PacketArrayCount
    );

BOOLEAN
NICCheckForHang(
    IN  PFDO_DATA     FdoData
    );

NTSTATUS
NICReset(
    IN PFDO_DATA FdoData
    );


VOID
NICGetDeviceInfSettings(
    IN OUT  PFDO_DATA   FdoData
    );

NTSTATUS
NICInitiateDmaTransfer(
    IN PFDO_DATA        FdoData,
    IN WDFREQUEST       Request
    );

typedef
USHORT
(*PREAD_PORT)(
    IN USHORT *Register
    );

typedef
VOID
(*PWRITE_PORT)(
    IN USHORT *Register,
    IN USHORT  Value
    );

#endif


