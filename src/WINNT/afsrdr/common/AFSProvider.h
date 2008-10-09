/*
 * Copyright (c) 2008 Kernel Drivers, LLC.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice,
 *   this list of conditions and the following disclaimer in the
 *   documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of Kernel Drivers, LLC nor the names of its
 *   contributors may be
 *   used to endorse or promote products derived from this software without
 *   specific prior written permission from Kernel Drivers, LLC.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AFS_PROVIDER_H
#define _AFS_PROVIDER_H


//
// Network provider interface header
//


//
// Redirector device name
//

#define AFS_RDR_DEVICE_NAME         L"\\Device\\AFSRedirector"

//
// Provider specific IOCtl requests
//

#define IOCTL_AFS_ADD_CONNECTION      CTL_CODE( FILE_DEVICE_DISK_FILE_SYSTEM, 0x2001, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AFS_CANCEL_CONNECTION   CTL_CODE( FILE_DEVICE_DISK_FILE_SYSTEM, 0x2002, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AFS_GET_CONNECTION      CTL_CODE( FILE_DEVICE_DISK_FILE_SYSTEM, 0x2003, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AFS_LIST_CONNECTIONS    CTL_CODE( FILE_DEVICE_DISK_FILE_SYSTEM, 0x2004, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _AFS_NETWORKPROVIDER_CONNECTION_CB
{

    WCHAR       LocalName;

    ULONG       ResourceType;

    USHORT      RemoteNameLength;

    WCHAR       RemoteName[ 1];

} AFSNetworkProviderConnectionCB;



#endif