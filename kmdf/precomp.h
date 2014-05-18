//
// precomp.h for pcidrv driver
//
#define WIN9X_COMPAT_SPINLOCK
#include <ntddk.h> 
#include <wdf.h>

typedef unsigned int        UINT;
typedef unsigned int        *PUINT;

#include <initguid.h> // required for GUID definitions
#include <wdmguid.h> // required for WMILIB_CONTEXT

#include <ntintsafe.h>


//
// Disable warnings that prevent our driver from compiling with /W4 MSC_WARNING_LEVEL
//
// Disable warning C4214: nonstandard extension used : bit field types other than int
// Disable warning C4201: nonstandard extension used : nameless struct/union
// Disable warning C4115: named type definition in parentheses
//
#pragma warning(disable:4214)
#pragma warning(disable:4201)
#pragma warning(disable:4115)

#pragma warning(default:4214)
#pragma warning(default:4201)
#pragma warning(default:4115)

#include "public.h"
#include "trace.h"
#include "nic_def.h"
#include "pcidrv.h"

__inline
USHORT
NICReadPortUShort (
    IN  USHORT * x
    )
{
    return READ_PORT_USHORT (x);
}
__inline
VOID
NICWritePortUShort (
    IN  USHORT * x,
    IN  USHORT   y
    )
{
    WRITE_PORT_USHORT (x,y);
}

__inline
USHORT
NICReadRegisterUShort (
    IN  USHORT * x
    )
{
    return READ_REGISTER_USHORT (x);
}

__inline
VOID
NICWriteRegisterUShort (
    IN  USHORT * x,
    IN  USHORT   y
    )
{
    WRITE_REGISTER_USHORT (x,y);
}


