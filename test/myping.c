/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    MYPING.C

Abstract:



Environment:

    usermode console application

--*/

#include "testapp.h"
#include "nuiouser.h"
#include <intsafe.h>

#define MAX_ECHO_PAY_LOAD 4

#define ETH_MAX_PACKET_SIZE         4
#define ETH_MIN_PACKET_SIZE         4

//
// For Read operation.
//
typedef struct _RCB {
    OVERLAPPED      Overlapped;
    char            Buffer[ETH_MAX_PACKET_SIZE];
    PDEVICE_INFO    DeviceInfo;
}RCB, *PRCB;

//
// For Write operation.
//
typedef struct _TCB {
    OVERLAPPED      Overlapped;
    char            *Buffer; // packet length is user specified.
    ULONG           BufferLength;
    PDEVICE_INFO    DeviceInfo;

}TCB, *PTCB;

unsigned short PacketId;

VOID
PostNextRead(
    RCB *pRCB
    );


VOID WriteComplete(DWORD dwError, DWORD dwBytesTransferred, LPOVERLAPPED pOvl)
{
   TCB* pTCB = (TCB *)pOvl;

    if (dwError) {
        if(dwError == ERROR_DEVICE_NOT_CONNECTED) {
            Display(TEXT("WriteComplete: Device not connected"));
        }
        else {
            Display(TEXT("WriteComplete: Error %x"), dwError);

        }
    }

    DisplayV(TEXT("Write Complete: %x"), dwBytesTransferred);
    HeapFree (GetProcessHeap(), 0, pTCB->Buffer);
    HeapFree (GetProcessHeap(), 0, pTCB);
}

VOID
ReadMacAddrComplete(
    DWORD dwError,
    DWORD dwBytesTransferred,
    LPOVERLAPPED pOvl
    )
{
    if (ERROR_OPERATION_ABORTED != dwError)
    {
        RCB* pRCB = (RCB *)pOvl;

        char *Buffer = pRCB->Buffer;
        PETH_HEADER ethHeader = (PETH_HEADER) Buffer;
        PARP_BODY   pBody;

        PDEVICE_INFO deviceInfo = pRCB->DeviceInfo;
        WCHAR unicodeIpAddr[MAX_LEN];
        char *ipAddr;

        DisplayV(TEXT("ReadMacAddrComplete: %x"), dwBytesTransferred);


        if(!ReadFileEx(pRCB->DeviceInfo->hDevice, pRCB->Buffer, sizeof(pRCB->Buffer),
            &pRCB->Overlapped, ReadMacAddrComplete)){
            Display(TEXT("ReadMacAddrComplete: ReadFileEx failed %x"), GetLastError());
        }

    }
}



VOID
ReadComplete(
    DWORD dwError,
    DWORD dwBytesTransferred,
    LPOVERLAPPED pOvl
    )
{
    if (ERROR_OPERATION_ABORTED != dwError)
    {
        RCB* pRCB = (RCB *)pOvl;
        DisplayV(TEXT("ReadComplete: %d"), dwBytesTransferred);
    }
}

VOID
PostNextRead(
    RCB *pRCB
    )
{

    if(!ReadFileEx(pRCB->DeviceInfo->hDevice, pRCB->Buffer, sizeof(pRCB->Buffer),
                &pRCB->Overlapped, ReadComplete))
    {
        Display(TEXT("Error in ReadFile: %x"), GetLastError());
    }

}


BOOLEAN
Ping(
    PDEVICE_INFO DeviceInfo
    )
{
    HANDLE hDevice = DeviceInfo->hDevice;
    char *icmpbuf;
    char *ipHeader;
    char *etherHeader;
    unsigned int icmpbuflen, packetlen;
    PTCB pTCB = NULL;

    packetlen = 4;

    // Add in the data size
    if(FAILED(UIntAdd(packetlen, DeviceInfo->PacketSize, &packetlen))) {
        Display(TEXT("Ping: UIntAdd Failed"));
        goto Error;
    }

    pTCB = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TCB));
    if(!pTCB){
        Display(TEXT("Ping: HeapAlloc Failed"));
        goto Error;
    }

    pTCB->Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, packetlen);
    if(!pTCB->Buffer){
        Display(TEXT("Ping: HeapAlloc Failed"));
        goto Error;
    }

    pTCB->BufferLength = packetlen;
    memset(&pTCB->Overlapped, 0, sizeof(OVERLAPPED));


    // Allocate the buffer that will contain the ICMP request
    etherHeader = pTCB->Buffer;
	etherHeader[0] = 1;
	etherHeader[1] = 2;
	etherHeader[2] = 3;
	etherHeader[3] = 4;
	
    if(!WriteFileEx(hDevice, etherHeader, packetlen, &pTCB->Overlapped, WriteComplete))
    {
            Display(TEXT("Ping: WriteFile failed %x"), GetLastError());
            goto Error;
    }

    return TRUE;

Error:

    if(pTCB) {
        if(pTCB->Buffer){
            HeapFree (GetProcessHeap(), 0, pTCB->Buffer);
        }
        HeapFree (GetProcessHeap(), 0, pTCB);
    }
    return FALSE;
}

DWORD
PingThread (
    PDEVICE_INFO DeviceInfo
    )
{
    RCB                 RCB;
    HANDLE              hDevice = DeviceInfo->hDevice;
    DWORD               status;


    Display(TEXT("Pinging %ws from %ws with %d bytes of data"),
                            DeviceInfo->UnicodeDestIp,
                            DeviceInfo->UnicodeSourceIp,
                            DeviceInfo->PacketSize);
    Sleep(1000);

    //
    // Every time a ping response is recevied, PingEvent will
    // be signalled.
    //
    DeviceInfo->PingEvent = CreateEvent(NULL, FALSE, FALSE, L"PingEvent");
    if (DeviceInfo->PingEvent == NULL) {
        Display(TEXT("CreateEvent failed 0x%x"), GetLastError());
        goto Exit;
    }

    DeviceInfo->NumberOfRequestSent = 0;
    DeviceInfo->Sleep = FALSE;
    DeviceInfo->TimeOut = 0;
    PacketId = 1;


    //
    // Initialize read control block. Reads requests are serialized.
    // Only one read is outstanding at any time. We allocate memory
    // for the RCB in the stack.
    //
    RCB.DeviceInfo = DeviceInfo;
    memset(&RCB.Overlapped, 0, sizeof(OVERLAPPED));

    //
    // Post a read buffer and start sending ping packets.
    //
    PostNextRead(&RCB);

    Ping(DeviceInfo);

    //
    // We will exit out of this thread if the number of ping count
    // exceeds the DEFAULT_SEND_COUNT or the main thread requested
    // us to exit by setting ExitThread value to TRUE.
    //
    while(DeviceInfo->NumberOfRequestSent < DEFAULT_SEND_COUNT
            && DeviceInfo->ExitThread == FALSE){

        status = WaitForSingleObjectEx(DeviceInfo->PingEvent, 1000, TRUE );
        if ( status == WAIT_OBJECT_0 ) {    // event fired, not timeout
            //
            // Probably we received a valid ping response from the target.
            //
            if(DeviceInfo->Sleep){
                Sleep(PING_SLEEP_TIME); // sleep for a sec before sending another ECHO
            }
            Ping(DeviceInfo);
            continue;
        }
        //
        // This is just a notification that either Read/Write operation got
        // completed. We will know the result of the acutal operation later
        // when the APC is called.
        //
        if( status == WAIT_IO_COMPLETION ) {
            continue;
        }
        if (status != WAIT_TIMEOUT){

            Display(TEXT("WaitForSingleObjectEx returned error %d"), status);
            break;
        }
        //
        // It seems like the wait timed out. So let us send another ping
        // and see if we get any response.
        //
        DeviceInfo->TimeOut++;
        if(DeviceInfo->TimeOut > MAX_PING_RETRY) {
            Display(TEXT("No response from the target"));
            break;
        }

        Ping(DeviceInfo);
    }

Exit:

    CloseHandle(DeviceInfo->hDevice);
    DeviceInfo->hDevice = INVALID_HANDLE_VALUE;
    DeviceInfo->ThreadHandle = NULL;
    if (DeviceInfo->PingEvent) {
        CloseHandle(DeviceInfo->PingEvent);
        DeviceInfo->PingEvent = NULL;
    }

    Display(TEXT("PingThread is exiting"));
    return 0;
}





