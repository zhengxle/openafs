
#define _WIN32_WINNT 0x0500
#define _CRT_SECURE_NO_DEPRECATE
#define _CRT_NON_CONFORMING_SWPRINTFS
#define UNICODE 1

#include <ntstatus.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <devioctl.h>

#include <tchar.h>
#include <winbase.h>
#include <winreg.h>

#include "..\\Common\\AFSUserCommon.h"
#include <RDRPrototypes.h>

#ifndef FlagOn
#define FlagOn(_F,_SF)        ((_F) & (_SF))
#endif

#ifndef BooleanFlagOn
#define BooleanFlagOn(F,SF)   ((BOOLEAN)(((F) & (SF)) != 0))
#endif

#ifndef SetFlag
#define SetFlag(_F,_SF)       ((_F) |= (_SF))
#endif

#ifndef ClearFlag
#define ClearFlag(_F,_SF)     ((_F) &= ~(_SF))
#endif

#define QuadAlign(Ptr) (                \
    ((((ULONG)(Ptr)) + 7) & 0xfffffff8) \
    )

HANDLE glThreadHandle[ 10];

UINT   glThreadHandleIndex = 0;

HANDLE glDevHandle = INVALID_HANDLE_VALUE;

static DWORD Exit = false;

extern "C" DWORD
RDR_Initialize(void)
{
    
    DWORD dwRet = 0;
		
    Exit = false;

    //
    // Launch our workers down to the
    // filters control device for processing requests
    //

    dwRet = RDR_ProcessWorkerThreads();

    return dwRet;
}

extern "C" DWORD
RDR_Shutdown(void)
{

    DWORD dwIndex = 0;

    Exit = true;

    //
    // Close all the worker thread handles
    //

    while( dwIndex < glThreadHandleIndex)
    {

        CloseHandle( glThreadHandle[ dwIndex]);

        dwIndex++;
    }

    if( glDevHandle != INVALID_HANDLE_VALUE)
    {

        CloseHandle( glDevHandle);
    }

    return 0;
}

//
// Here we launch the worker threaqds for the given volume
//

DWORD
RDR_ProcessWorkerThreads()
{
    DWORD WorkerID;
    HANDLE hEvent;
    DWORD index = 0;
    DWORD bytesReturned = 0;
    DWORD dwCacheFileInfo;
    AFSCacheFileInfo * cacheFileInfo = NULL;
    DWORD dwErr;

    if (dwErr = RDR_SetInitParams(&cacheFileInfo, &dwCacheFileInfo))
        return dwErr;

    glDevHandle = CreateFile( AFS_SYMLINK_W,
			      GENERIC_READ | GENERIC_WRITE,
			      FILE_SHARE_READ | FILE_SHARE_WRITE,
			      NULL,
			      OPEN_EXISTING,
			      FILE_FLAG_OVERLAPPED,
			      NULL);

    if( glDevHandle == INVALID_HANDLE_VALUE)
    {
        free(cacheFileInfo);
        return GetLastError();
    }

    //
    // Now call down to initialize the pool.
    //

    if( !DeviceIoControl( glDevHandle,
			  IOCTL_AFS_INITIALIZE_CONTROL_DEVICE,
                          NULL,
                          0,
                          NULL,
                          0,
                          &bytesReturned,
			  NULL))
    {

        CloseHandle( glDevHandle);

        glDevHandle = NULL;

        free(cacheFileInfo);

        return GetLastError();
    }

    //
    // OK, now launch the workers
    //

    hEvent = CreateEvent( NULL,
			  TRUE,
			  FALSE,
			  NULL);

    //
    // Here we create a pool of 5 worker threads but you can create the pool with as many requests
    // as you want
    //

    while( index < 5)
    {

        glThreadHandle[ glThreadHandleIndex] = CreateThread( NULL,
							     0,
							     RDR_RequestWorkerThread,
							     (void *)hEvent,
							     0,
							     &WorkerID);

        if( glThreadHandle[ glThreadHandleIndex] != NULL)
        {

            //
            // Wait for the thread to signal it is ready for processing
            //

            WaitForSingleObject( hEvent,
				 INFINITE);

            glThreadHandleIndex++;

            ResetEvent( hEvent);
        }
        else
        {

            //
            // Perform cleanup specific to your application
            //

        }

        index++;
    }

    if( !DeviceIoControl( glDevHandle,
			  IOCTL_AFS_INITIALIZE_REDIRECTOR_DEVICE,
			  cacheFileInfo,
                          dwCacheFileInfo,
                          NULL,
                          0,
                          &bytesReturned,
			  NULL))
    {

        CloseHandle( glDevHandle);

        glDevHandle = NULL;

        free(cacheFileInfo);

        return GetLastError();
    }

    free(cacheFileInfo);

    return 0;
}

//
// Entry point for the worker thread
//

DWORD 
WINAPI 
RDR_RequestWorkerThread( LPVOID lpParameter)
{

    HANDLE hDevHandle = NULL;
    DWORD bytesReturned;
    AFSCommRequest *requestBuffer;
    bool bError = false;
    HANDLE hEvent = (HANDLE)lpParameter;

    //
    // We use the global handle to the control device instance
    //

    hDevHandle = glDevHandle;

    //
    // Allocate a request buffer.
    //

    requestBuffer = (AFSCommRequest *)malloc( sizeof( AFSCommRequest) + AFS_PAYLOAD_BUFFER_SIZE);

    if( requestBuffer)
    {

	memset( requestBuffer, '\0', sizeof( AFSCommRequest) + AFS_PAYLOAD_BUFFER_SIZE);

        //
        // Here we simply signal back to the main thread that we ahve started
        //

        SetEvent( hEvent);

        //
        // Process requests until we are told to stop
        //

        while( !Exit)
	{

            memset( requestBuffer, '\0', sizeof( AFSCommRequest) + AFS_PAYLOAD_BUFFER_SIZE);

            if( !DeviceIoControl( hDevHandle,
				  IOCTL_AFS_PROCESS_IRP_REQUEST,
				  NULL,
				  0,
				  (void *)requestBuffer,
				  sizeof( AFSCommRequest) + AFS_PAYLOAD_BUFFER_SIZE,
				  &bytesReturned,
				  NULL))
            {

                //
                // Error condition back from driver
                //

                break;
            }

            //
            // Go process the request
            //

            RDR_ProcessRequest( requestBuffer);
        }

	free( requestBuffer);
    }

    ExitThread( 0);

    return 0;
}

//
// This is the entry point for the worker threads to process the request from the TC Filter driver
//

void
RDR_ProcessRequest( AFSCommRequest *RequestBuffer)
{

    DWORD       	bytesReturned;
    DWORD       	result = 0;
    ULONG       	ulIndex = 0;
    ULONG       	ulCreateFlags = 0;
    AFSCommResult *     pResultCB = NULL;
    AFSCommResult 	stResultCB;
    DWORD       	dwResultBufferLength = 0;
    AFSSetFileExtentsCB * SetFileExtentsResultCB = NULL;
    WCHAR 		wchBuffer[256];
    cm_user_t *         userp = NULL;

    userp = RDR_UserFromCommRequest(RequestBuffer);

    //
    // Build up the string to display based on the request type. 
    //

    switch( RequestBuffer->RequestType)
    {
        
        case AFS_REQUEST_TYPE_DIR_ENUM:
        {

            AFSDirQueryCB *pQueryCB = (AFSDirQueryCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);
            
            swprintf( wchBuffer,
                      L"ProcessRequest Processing AFS_REQUEST_TYPE_DIR_ENUM Index %08lX\n",
                      RequestBuffer->RequestIndex);
                        
            OutputDebugString( wchBuffer);

            //
            // Here is where the content of the specific directory is enumerated.
            //

            RDR_EnumerateDirectory( userp, RequestBuffer->FileId,
				    pQueryCB,
				    RequestBuffer->ResultBufferLength,
				    &pResultCB);
            break;
        }

        case AFS_REQUEST_TYPE_EVAL_TARGET_BY_ID:
        {
            AFSEvalTargetCB *pEvalTargetCB = (AFSEvalTargetCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer,
                      L"ProcessRequest Processing AFS_REQUEST_TYPE_EVAL_TARGET_BY_ID Index %08lX\n",
                      RequestBuffer->RequestIndex);
                        
            OutputDebugString( wchBuffer);

            //
            // Here is where the specified node is evaluated.
            //

            RDR_EvaluateNodeByID( userp, pEvalTargetCB->ParentId,
                                  RequestBuffer->FileId,
                                  RequestBuffer->ResultBufferLength,
                                  &pResultCB);
            break;
        }

        case AFS_REQUEST_TYPE_EVAL_TARGET_BY_NAME:
        {
            AFSEvalTargetCB *pEvalTargetCB = (AFSEvalTargetCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer,
                      L"ProcessRequest Processing AFS_REQUEST_TYPE_EVAL_TARGET_BY_NAME Index %08lX\n",
                      RequestBuffer->RequestIndex);

            OutputDebugString( wchBuffer);

            //
            // Here is where the specified node is evaluated.
            //

            RDR_EvaluateNodeByName( userp, pEvalTargetCB->ParentId,
                                    RequestBuffer->Name,
                                    RequestBuffer->NameLength,
                                    RequestBuffer->RequestFlags & AFS_REQUEST_FLAG_CASE_SENSITIVE ? 1 : 0,
                                    RequestBuffer->ResultBufferLength,
                                    &pResultCB);
            break;
        }

        case AFS_REQUEST_TYPE_CREATE_FILE:
        {

            AFSFileCreateCB *pCreateCB = (AFSFileCreateCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);
    
            WCHAR wchFileName[ 256];

            memset( wchFileName, '\0', 256 * sizeof( WCHAR));

            memcpy( wchFileName,
                    RequestBuffer->Name,
                    RequestBuffer->NameLength);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_CREATE_FILE File %s\n", wchFileName);

            OutputDebugString( wchBuffer);

            RDR_CreateFileEntry( userp, 
                                 RequestBuffer->Name,
				 RequestBuffer->NameLength,
				 pCreateCB,
				 RequestBuffer->ResultBufferLength,
				 &pResultCB);
    
            break;
        }

        case AFS_REQUEST_TYPE_UPDATE_FILE:
        {

            AFSFileUpdateCB *pUpdateCB = (AFSFileUpdateCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_UPDATE_FILE File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_UpdateFileEntry( userp, RequestBuffer->FileId,
				 pUpdateCB,
				 &pResultCB);

            break;
        }

        case AFS_REQUEST_TYPE_DELETE_FILE:
        {

            AFSFileDeleteCB *pDeleteCB = (AFSFileDeleteCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);
    
            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_DELETE_FILE %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_DeleteFileEntry( userp, 
                                 pDeleteCB->ParentId,
                                 RequestBuffer->Name,
				 RequestBuffer->NameLength,
				 &pResultCB);

            break;
        }

        case AFS_REQUEST_TYPE_RENAME_FILE:
        {

            AFSFileRenameCB *pFileRenameCB = (AFSFileRenameCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_RENAME_FILE File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_RenameFileEntry( userp, RequestBuffer->FileId,
				 pFileRenameCB,
				 RequestBuffer->ResultBufferLength,
				 &pResultCB);

            break;
        }

        case AFS_REQUEST_TYPE_REQUEST_FILE_EXTENTS:
        {

            AFSRequestExtentsCB *pFileRequestExtentsCB = (AFSRequestExtentsCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_REQUEST_FILE_EXTENTS File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            if (BooleanFlagOn( RequestBuffer->RequestFlags, AFS_REQUEST_FLAG_SYNCHRONOUS))
                RDR_RequestFileExtentsSync( userp, RequestBuffer->FileId,
                                            pFileRequestExtentsCB,
                                            RequestBuffer->ResultBufferLength,
                                            &pResultCB);
            else
                RDR_RequestFileExtentsAsync( userp, RequestBuffer->FileId, 
                                             pFileRequestExtentsCB,
                                             &dwResultBufferLength,
                                             &SetFileExtentsResultCB );

            break;
        }

        case AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS:
        {

            AFSReleaseExtentsCB *pFileReleaseExtentsCB = (AFSReleaseExtentsCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_RELEASE_FILE_EXTENTS File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_ReleaseFileExtents( userp, RequestBuffer->FileId,
				    pFileReleaseExtentsCB,
				    RequestBuffer->ResultBufferLength,
				    &pResultCB);

            break;
        }

        case AFS_REQUEST_TYPE_FLUSH_FILE:
        {
//            AFSFileFlushCB *pFileFlushCB = (AFSFileFlushCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_FLUSH_FILE File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_FlushFileEntry( userp, RequestBuffer->FileId,
                                // pFileFlushCB,
                                RequestBuffer->ResultBufferLength,
                                &pResultCB);
            break;
        }

        case AFS_REQUEST_TYPE_OPEN_FILE:
        {
            AFSFileOpenCB *pFileOpenCB = (AFSFileOpenCB *)((char *)RequestBuffer->Name + RequestBuffer->DataOffset);

            swprintf( wchBuffer, L"ProcessRequest Processing AFS_REQUEST_TYPE_OPEN_FILE File %08lX.%08lX.%08lX.%08lX\n", 
		      RequestBuffer->FileId.Cell, RequestBuffer->FileId.Volume, 
		      RequestBuffer->FileId.Vnode, RequestBuffer->FileId.Unique);

            OutputDebugString( wchBuffer);

            RDR_OpenFileEntry( userp, RequestBuffer->FileId,
                               pFileOpenCB,
                               RequestBuffer->ResultBufferLength,
                               &pResultCB);

            break;
        }

    default:

            break;
    }

    if (userp) 
        RDR_ReleaseUser(userp);

    if( BooleanFlagOn( RequestBuffer->RequestFlags, AFS_REQUEST_FLAG_SYNCHRONOUS))
    {
	if (pResultCB == NULL) {
	    /* We failed probably due to a memory allocation error */
	    pResultCB = &stResultCB;
	    pResultCB->ResultStatus = STATUS_NO_MEMORY;
	}

        //
        // This is how the filter associates the response information passed in the IOCtl below to the 
        // original call. This request index is setup by the filter and should not be modified, otherwise the
        // filter will not be able to locate the request in its internal queue and the blocking thread will
        // not be awakened
        //

        pResultCB->RequestIndex = RequestBuffer->RequestIndex;

        swprintf( wchBuffer,
                  L"ProcessRequest Responding to request index %08lX Length %08lX\n",
                  pResultCB->RequestIndex,
                  pResultCB->ResultBufferLength);
                        
        OutputDebugString( wchBuffer);

        //
        // Now post the result back to the driver.
        //

        if( !DeviceIoControl( glDevHandle,
			      IOCTL_AFS_PROCESS_IRP_RESULT,
			      (void *)pResultCB,
                              sizeof( AFSCommResult) + pResultCB->ResultBufferLength,
			      (void *)NULL,
			      0,
			      &bytesReturned,
			      NULL))
        {

            MessageBox( NULL, L"Failed to post IOCtl", L"Error", MB_OK);
        }

    }
    else if (RequestBuffer->RequestType == AFS_REQUEST_TYPE_REQUEST_FILE_EXTENTS) {

        swprintf( wchBuffer,
                  L"ProcessRequest Responding Asynchronously to REQUEST_FILE_EXTENTS request index %08lX\n",
                  RequestBuffer->RequestIndex);
                        
        OutputDebugString( wchBuffer);

        if( !DeviceIoControl( glDevHandle,
			      IOCTL_AFS_SET_FILE_EXTENTS,
			      (void *)SetFileExtentsResultCB,
                              dwResultBufferLength,
			      (void *)NULL,
			      0,
			      &bytesReturned,
			      NULL))
        {

            MessageBox( NULL, L"Failed to post IOCtl", L"Error", MB_OK);
        }

        free(SetFileExtentsResultCB);
    } 
    else {

        swprintf( wchBuffer,
                  L"ProcessRequest Not responding to async request index %08lX\n",
                  RequestBuffer->RequestIndex);
                        
        OutputDebugString( wchBuffer);
    }

    if( pResultCB && pResultCB != &stResultCB)
    {

	free( pResultCB);
    }
    return;
}
