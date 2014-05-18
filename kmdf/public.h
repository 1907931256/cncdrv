/*++
Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid for toaster device class.
// This GUID is used to register (IoRegisterDeviceInterface)
// an instance of an interface so that user application
// can control the toaster device.
//

DEFINE_GUID (GUID_DEVINTERFACE_PCIDRV,
    0xb74cfec2, 0x9366, 0x454a, 0xba, 0x71, 0x7c, 0x27, 0xb5, 0x14, 0x70, 0xa4);
// {B74CFEC2-9366-454a-BA71-7C27B51470A4}

//
// GUID definition are required to be outside of header inclusion pragma to avoid
// error during precompiled headers.
//

