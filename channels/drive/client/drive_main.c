/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * File System Virtual Channel
 *
 * Copyright 2010-2011 Vic Lee
 * Copyright 2010-2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2015 Thincast Technologies GmbH
 * Copyright 2015 DI (FH) Martin Haimberger <martin.haimberger@thincast.com>
 * Copyright 2016 David PHAM-VAN <d.phamvan@inuvika.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <freerdp/config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>
#include <winpr/assert.h>
#include <winpr/path.h>
#include <winpr/file.h>
#include <winpr/string.h>
#include <winpr/synch.h>
#include <winpr/thread.h>
#include <winpr/stream.h>
#include <winpr/environment.h>
#include <winpr/interlocked.h>
#include <winpr/collections.h>
#include <winpr/shell.h>

#include <freerdp/freerdp.h>
#include <freerdp/channels/rdpdr.h>

#include "drive_file.h"

typedef struct
{
	DEVICE device;

	WCHAR* path;
	BOOL automount;
	UINT32 PathLength;
	wListDictionary* files;

	HANDLE thread;
	BOOL async;
	wMessageQueue* IrpQueue;

	DEVMAN* devman;

	rdpContext* rdpcontext;
} DRIVE_DEVICE;

static NTSTATUS drive_map_windows_err(DWORD fs_errno)
{
	NTSTATUS rc = 0;

	/* try to return NTSTATUS version of error code */

	switch (fs_errno)
	{
		case STATUS_SUCCESS:
			rc = STATUS_SUCCESS;
			break;

		case ERROR_ACCESS_DENIED:
		case ERROR_SHARING_VIOLATION:
			rc = STATUS_ACCESS_DENIED;
			break;

		case ERROR_FILE_NOT_FOUND:
			rc = STATUS_NO_SUCH_FILE;
			break;

		case ERROR_BUSY_DRIVE:
			rc = STATUS_DEVICE_BUSY;
			break;

		case ERROR_INVALID_DRIVE:
			rc = STATUS_NO_SUCH_DEVICE;
			break;

		case ERROR_NOT_READY:
			rc = STATUS_NO_SUCH_DEVICE;
			break;

		case ERROR_FILE_EXISTS:
		case ERROR_ALREADY_EXISTS:
			rc = STATUS_OBJECT_NAME_COLLISION;
			break;

		case ERROR_INVALID_NAME:
			rc = STATUS_NO_SUCH_FILE;
			break;

		case ERROR_INVALID_HANDLE:
			rc = STATUS_INVALID_HANDLE;
			break;

		case ERROR_NO_MORE_FILES:
			rc = STATUS_NO_MORE_FILES;
			break;

		case ERROR_DIRECTORY:
			rc = STATUS_NOT_A_DIRECTORY;
			break;

		case ERROR_PATH_NOT_FOUND:
			rc = STATUS_OBJECT_PATH_NOT_FOUND;
			break;

		case ERROR_DIR_NOT_EMPTY:
			rc = STATUS_DIRECTORY_NOT_EMPTY;
			break;

		default:
			rc = STATUS_UNSUCCESSFUL;
			WLog_ERR(TAG, "Error code not found: %" PRIu32 "", fs_errno);
			break;
	}

	return rc;
}

static DRIVE_FILE* drive_get_file_by_id(DRIVE_DEVICE* drive, UINT32 id)
{
	DRIVE_FILE* file = NULL;
	void* key = (void*)(size_t)id;

	if (!drive)
		return NULL;

	file = (DRIVE_FILE*)ListDictionary_GetItemValue(drive->files, key);
	return file;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_create(DRIVE_DEVICE* drive, IRP* irp)
{
	UINT32 FileId = 0;
	DRIVE_FILE* file = NULL;
	BYTE Information = 0;
	const WCHAR* path = NULL;

	if (!drive || !irp || !irp->devman)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 6 * 4 + 8))
		return ERROR_INVALID_DATA;

	const uint32_t DesiredAccess = Stream_Get_UINT32(irp->input);
	const uint64_t allocationSize = Stream_Get_UINT64(irp->input);
	const uint32_t FileAttributes = Stream_Get_UINT32(irp->input);
	const uint32_t SharedAccess = Stream_Get_UINT32(irp->input);
	const uint32_t CreateDisposition = Stream_Get_UINT32(irp->input);
	const uint32_t CreateOptions = Stream_Get_UINT32(irp->input);
	const uint32_t PathLength = Stream_Get_UINT32(irp->input);

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, PathLength))
		return ERROR_INVALID_DATA;

	path = Stream_ConstPointer(irp->input);
	FileId = irp->devman->id_sequence++;
	file = drive_file_new(drive->path, path, PathLength / sizeof(WCHAR), FileId, DesiredAccess,
	                      CreateDisposition, CreateOptions, FileAttributes, SharedAccess);

	if (!file)
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
		FileId = 0;
		Information = 0;
	}
	else
	{
		void* key = (void*)(size_t)file->id;

		if (!ListDictionary_Add(drive->files, key, file))
		{
			WLog_ERR(TAG, "ListDictionary_Add failed!");
			return ERROR_INTERNAL_ERROR;
		}

		switch (CreateDisposition)
		{
			case FILE_SUPERSEDE:
			case FILE_OPEN:
			case FILE_CREATE:
			case FILE_OVERWRITE:
				Information = FILE_SUPERSEDED;
				break;

			case FILE_OPEN_IF:
				Information = FILE_OPENED;
				break;

			case FILE_OVERWRITE_IF:
				Information = FILE_OVERWRITTEN;
				break;

			default:
				Information = 0;
				break;
		}

		if (allocationSize > 0)
		{
			const BYTE buffer[] = { '\0' };
			if (!drive_file_seek(file, allocationSize - sizeof(buffer)))
				return ERROR_INTERNAL_ERROR;
			if (!drive_file_write(file, buffer, sizeof(buffer)))
				return ERROR_INTERNAL_ERROR;
		}
	}

	Stream_Write_UINT32(irp->output, FileId);
	Stream_Write_UINT8(irp->output, Information);

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_close(DRIVE_DEVICE* drive, IRP* irp)
{
	void* key = NULL;
	DRIVE_FILE* file = NULL;

	if (!drive || !irp || !irp->output)
		return ERROR_INVALID_PARAMETER;

	file = drive_get_file_by_id(drive, irp->FileId);
	key = (void*)(size_t)irp->FileId;

	if (!file)
		irp->IoStatus = STATUS_UNSUCCESSFUL;
	else
	{
		ListDictionary_Take(drive->files, key);

		if (drive_file_free(file))
			irp->IoStatus = STATUS_SUCCESS;
		else
			irp->IoStatus = drive_map_windows_err(GetLastError());
	}

	Stream_Zero(irp->output, 5); /* Padding(5) */

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_read(DRIVE_DEVICE* drive, IRP* irp)
{
	DRIVE_FILE* file = NULL;
	UINT32 Length = 0;
	UINT64 Offset = 0;

	if (!drive || !irp || !irp->output)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 12))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, Length);
	Stream_Read_UINT64(irp->input, Offset);
	file = drive_get_file_by_id(drive, irp->FileId);

	if (!file)
	{
		irp->IoStatus = STATUS_UNSUCCESSFUL;
		Length = 0;
	}
	else if (!drive_file_seek(file, Offset))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
		Length = 0;
	}

	if (!Stream_EnsureRemainingCapacity(irp->output, Length + 4))
	{
		WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
		return ERROR_INTERNAL_ERROR;
	}
	else if (Length == 0)
		Stream_Write_UINT32(irp->output, 0);
	else
	{
		BYTE* buffer = Stream_PointerAs(irp->output, BYTE) + sizeof(UINT32);

		if (!drive_file_read(file, buffer, &Length))
		{
			irp->IoStatus = drive_map_windows_err(GetLastError());
			Stream_Write_UINT32(irp->output, 0);
		}
		else
		{
			Stream_Write_UINT32(irp->output, Length);
			Stream_Seek(irp->output, Length);
		}
	}

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_write(DRIVE_DEVICE* drive, IRP* irp)
{
	DRIVE_FILE* file = NULL;
	UINT32 Length = 0;
	UINT64 Offset = 0;

	if (!drive || !irp || !irp->input || !irp->output)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 32))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, Length);
	Stream_Read_UINT64(irp->input, Offset);
	Stream_Seek(irp->input, 20); /* Padding */
	const void* ptr = Stream_ConstPointer(irp->input);
	if (!Stream_SafeSeek(irp->input, Length))
		return ERROR_INVALID_DATA;
	file = drive_get_file_by_id(drive, irp->FileId);

	if (!file)
	{
		irp->IoStatus = STATUS_UNSUCCESSFUL;
		Length = 0;
	}
	else if (!drive_file_seek(file, Offset))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
		Length = 0;
	}
	else if (!drive_file_write(file, ptr, Length))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
		Length = 0;
	}

	Stream_Write_UINT32(irp->output, Length);
	Stream_Write_UINT8(irp->output, 0); /* Padding */

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_query_information(DRIVE_DEVICE* drive, IRP* irp)
{
	DRIVE_FILE* file = NULL;
	UINT32 FsInformationClass = 0;

	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 4))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, FsInformationClass);
	file = drive_get_file_by_id(drive, irp->FileId);

	if (!file)
	{
		irp->IoStatus = STATUS_UNSUCCESSFUL;
	}
	else if (!drive_file_query_information(file, FsInformationClass, irp->output))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
	}

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_set_information(DRIVE_DEVICE* drive, IRP* irp)
{
	DRIVE_FILE* file = NULL;
	UINT32 FsInformationClass = 0;
	UINT32 Length = 0;

	if (!drive || !irp || !irp->input || !irp->output)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 32))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, FsInformationClass);
	Stream_Read_UINT32(irp->input, Length);
	Stream_Seek(irp->input, 24); /* Padding */
	file = drive_get_file_by_id(drive, irp->FileId);

	if (!file)
	{
		irp->IoStatus = STATUS_UNSUCCESSFUL;
	}
	else if (!drive_file_set_information(file, FsInformationClass, Length, irp->input))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
	}

	Stream_Write_UINT32(irp->output, Length);

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_query_volume_information(DRIVE_DEVICE* drive, IRP* irp)
{
	UINT32 FsInformationClass = 0;
	wStream* output = NULL;
	DWORD lpSectorsPerCluster = 0;
	DWORD lpBytesPerSector = 0;
	DWORD lpNumberOfFreeClusters = 0;
	DWORD lpTotalNumberOfClusters = 0;
	WIN32_FILE_ATTRIBUTE_DATA wfad = { 0 };
	WCHAR LabelBuffer[32] = { 0 };

	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	output = irp->output;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 4))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, FsInformationClass);
	GetDiskFreeSpaceW(drive->path, &lpSectorsPerCluster, &lpBytesPerSector, &lpNumberOfFreeClusters,
	                  &lpTotalNumberOfClusters);

	switch (FsInformationClass)
	{
		case FileFsVolumeInformation:
		{
			/* http://msdn.microsoft.com/en-us/library/cc232108.aspx */
			const WCHAR* volumeLabel =
			    InitializeConstWCharFromUtf8("FREERDP", LabelBuffer, ARRAYSIZE(LabelBuffer));
			const size_t volumeLabelLen = (_wcslen(volumeLabel) + 1) * sizeof(WCHAR);
			const size_t length = 17ul + volumeLabelLen;

			if ((length > UINT32_MAX) || (volumeLabelLen > UINT32_MAX))
				return CHANNEL_RC_NO_BUFFER;

			Stream_Write_UINT32(output, (UINT32)length); /* Length */

			if (!Stream_EnsureRemainingCapacity(output, length))
			{
				WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
				return CHANNEL_RC_NO_MEMORY;
			}

			GetFileAttributesExW(drive->path, GetFileExInfoStandard, &wfad);
			Stream_Write_UINT32(output, wfad.ftCreationTime.dwLowDateTime); /* VolumeCreationTime */
			Stream_Write_UINT32(output,
			                    wfad.ftCreationTime.dwHighDateTime);      /* VolumeCreationTime */
			Stream_Write_UINT32(output, lpNumberOfFreeClusters & 0xffff); /* VolumeSerialNumber */
			Stream_Write_UINT32(output, (UINT32)volumeLabelLen);          /* VolumeLabelLength */
			Stream_Write_UINT8(output, 0);                                /* SupportsObjects */
			/* Reserved(1), MUST NOT be added! */
			Stream_Write(output, volumeLabel, volumeLabelLen); /* VolumeLabel (Unicode) */
		}
		break;

		case FileFsSizeInformation:
			/* http://msdn.microsoft.com/en-us/library/cc232107.aspx */
			Stream_Write_UINT32(output, 24); /* Length */

			if (!Stream_EnsureRemainingCapacity(output, 24))
			{
				WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
				return CHANNEL_RC_NO_MEMORY;
			}

			Stream_Write_UINT64(output, lpTotalNumberOfClusters); /* TotalAllocationUnits */
			Stream_Write_UINT64(output, lpNumberOfFreeClusters);  /* AvailableAllocationUnits */
			Stream_Write_UINT32(output, lpSectorsPerCluster);     /* SectorsPerAllocationUnit */
			Stream_Write_UINT32(output, lpBytesPerSector);        /* BytesPerSector */
			break;

		case FileFsAttributeInformation:
		{
			/* http://msdn.microsoft.com/en-us/library/cc232101.aspx */
			const WCHAR* diskType =
			    InitializeConstWCharFromUtf8("FAT32", LabelBuffer, ARRAYSIZE(LabelBuffer));
			const size_t diskTypeLen = (_wcslen(diskType) + 1) * sizeof(WCHAR);
			const size_t length = 12ul + diskTypeLen;

			if ((length > UINT32_MAX) || (diskTypeLen > UINT32_MAX))
				return CHANNEL_RC_NO_BUFFER;

			Stream_Write_UINT32(output, (UINT32)length); /* Length */

			if (!Stream_EnsureRemainingCapacity(output, length))
			{
				WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
				return CHANNEL_RC_NO_MEMORY;
			}

			Stream_Write_UINT32(output, FILE_CASE_SENSITIVE_SEARCH | FILE_CASE_PRESERVED_NAMES |
			                                FILE_UNICODE_ON_DISK); /* FileSystemAttributes */
			Stream_Write_UINT32(output, MAX_PATH);                 /* MaximumComponentNameLength */
			Stream_Write_UINT32(output, (UINT32)diskTypeLen);      /* FileSystemNameLength */
			Stream_Write(output, diskType, diskTypeLen);           /* FileSystemName (Unicode) */
		}
		break;

		case FileFsFullSizeInformation:
			/* http://msdn.microsoft.com/en-us/library/cc232104.aspx */
			Stream_Write_UINT32(output, 32); /* Length */

			if (!Stream_EnsureRemainingCapacity(output, 32))
			{
				WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
				return CHANNEL_RC_NO_MEMORY;
			}

			Stream_Write_UINT64(output, lpTotalNumberOfClusters); /* TotalAllocationUnits */
			Stream_Write_UINT64(output,
			                    lpNumberOfFreeClusters); /* CallerAvailableAllocationUnits */
			Stream_Write_UINT64(output, lpNumberOfFreeClusters); /* AvailableAllocationUnits */
			Stream_Write_UINT32(output, lpSectorsPerCluster);    /* SectorsPerAllocationUnit */
			Stream_Write_UINT32(output, lpBytesPerSector);       /* BytesPerSector */
			break;

		case FileFsDeviceInformation:
			/* http://msdn.microsoft.com/en-us/library/cc232109.aspx */
			Stream_Write_UINT32(output, 8); /* Length */

			if (!Stream_EnsureRemainingCapacity(output, 8))
			{
				WLog_ERR(TAG, "Stream_EnsureRemainingCapacity failed!");
				return CHANNEL_RC_NO_MEMORY;
			}

			Stream_Write_UINT32(output, FILE_DEVICE_DISK); /* DeviceType */
			Stream_Write_UINT32(output, 0);                /* Characteristics */
			break;

		default:
			WLog_WARN(TAG, "Unhandled FSInformationClass %s [0x%08" PRIx32 "]",
			          FSInformationClass2Tag(FsInformationClass), FsInformationClass);
			irp->IoStatus = STATUS_UNSUCCESSFUL;
			Stream_Write_UINT32(output, 0); /* Length */
			break;
	}

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/* http://msdn.microsoft.com/en-us/library/cc241518.aspx */

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_silent_ignore(DRIVE_DEVICE* drive, IRP* irp)
{
	if (!drive || !irp || !irp->output)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 4))
		return ERROR_INVALID_DATA;

	const uint32_t FsInformationClass = Stream_Get_UINT32(irp->input);
	WLog_VRB(TAG, "Silently ignore FSInformationClass %s [0x%08" PRIx32 "]",
	         FSInformationClass2Tag(FsInformationClass), FsInformationClass);
	Stream_Write_UINT32(irp->output, 0); /* Length */
	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_query_directory(DRIVE_DEVICE* drive, IRP* irp)
{
	const WCHAR* path = NULL;
	DRIVE_FILE* file = NULL;
	BYTE InitialQuery = 0;
	UINT32 PathLength = 0;
	UINT32 FsInformationClass = 0;

	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, 32))
		return ERROR_INVALID_DATA;

	Stream_Read_UINT32(irp->input, FsInformationClass);
	Stream_Read_UINT8(irp->input, InitialQuery);
	Stream_Read_UINT32(irp->input, PathLength);
	Stream_Seek(irp->input, 23); /* Padding */
	path = Stream_ConstPointer(irp->input);
	if (!Stream_CheckAndLogRequiredLength(TAG, irp->input, PathLength))
		return ERROR_INVALID_DATA;

	file = drive_get_file_by_id(drive, irp->FileId);

	if (file == NULL)
	{
		irp->IoStatus = STATUS_UNSUCCESSFUL;
		Stream_Write_UINT32(irp->output, 0); /* Length */
	}
	else if (!drive_file_query_directory(file, FsInformationClass, InitialQuery, path,
	                                     PathLength / sizeof(WCHAR), irp->output))
	{
		irp->IoStatus = drive_map_windows_err(GetLastError());
	}

	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_directory_control(DRIVE_DEVICE* drive, IRP* irp)
{
	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	switch (irp->MinorFunction)
	{
		case IRP_MN_QUERY_DIRECTORY:
			return drive_process_irp_query_directory(drive, irp);

		case IRP_MN_NOTIFY_CHANGE_DIRECTORY: /* TODO */
			return irp->Discard(irp);

		default:
			irp->IoStatus = STATUS_NOT_SUPPORTED;
			Stream_Write_UINT32(irp->output, 0); /* Length */
			WINPR_ASSERT(irp->Complete);
			return irp->Complete(irp);
	}

	return CHANNEL_RC_OK;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp_device_control(DRIVE_DEVICE* drive, IRP* irp)
{
	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	Stream_Write_UINT32(irp->output, 0); /* OutputBufferLength */
	WINPR_ASSERT(irp->Complete);
	return irp->Complete(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_process_irp(DRIVE_DEVICE* drive, IRP* irp)
{
	UINT error = 0;

	if (!drive || !irp)
		return ERROR_INVALID_PARAMETER;

	irp->IoStatus = STATUS_SUCCESS;

	switch (irp->MajorFunction)
	{
		case IRP_MJ_CREATE:
			error = drive_process_irp_create(drive, irp);
			break;

		case IRP_MJ_CLOSE:
			error = drive_process_irp_close(drive, irp);
			break;

		case IRP_MJ_READ:
			error = drive_process_irp_read(drive, irp);
			break;

		case IRP_MJ_WRITE:
			error = drive_process_irp_write(drive, irp);
			break;

		case IRP_MJ_QUERY_INFORMATION:
			error = drive_process_irp_query_information(drive, irp);
			break;

		case IRP_MJ_SET_INFORMATION:
			error = drive_process_irp_set_information(drive, irp);
			break;

		case IRP_MJ_QUERY_VOLUME_INFORMATION:
			error = drive_process_irp_query_volume_information(drive, irp);
			break;

		case IRP_MJ_LOCK_CONTROL:
			error = drive_process_irp_silent_ignore(drive, irp);
			break;

		case IRP_MJ_DIRECTORY_CONTROL:
			error = drive_process_irp_directory_control(drive, irp);
			break;

		case IRP_MJ_DEVICE_CONTROL:
			error = drive_process_irp_device_control(drive, irp);
			break;

		default:
			irp->IoStatus = STATUS_NOT_SUPPORTED;
			WINPR_ASSERT(irp->Complete);
			error = irp->Complete(irp);
			break;
	}

	return error;
}

static BOOL drive_poll_run(DRIVE_DEVICE* drive, IRP* irp)
{
	WINPR_ASSERT(drive);

	if (irp)
	{
		const UINT error = drive_process_irp(drive, irp);
		if (error)
		{
			WLog_ERR(TAG, "drive_process_irp failed with error %" PRIu32 "!", error);
			return FALSE;
		}
	}

	return TRUE;
}

static DWORD WINAPI drive_thread_func(LPVOID arg)
{
	DRIVE_DEVICE* drive = (DRIVE_DEVICE*)arg;
	UINT error = CHANNEL_RC_OK;

	if (!drive)
	{
		error = ERROR_INVALID_PARAMETER;
		goto fail;
	}

	while (1)
	{
		if (!MessageQueue_Wait(drive->IrpQueue))
		{
			WLog_ERR(TAG, "MessageQueue_Wait failed!");
			error = ERROR_INTERNAL_ERROR;
			break;
		}

		if (MessageQueue_Size(drive->IrpQueue) < 1)
			continue;

		wMessage message = { 0 };
		if (!MessageQueue_Peek(drive->IrpQueue, &message, TRUE))
		{
			WLog_ERR(TAG, "MessageQueue_Peek failed!");
			continue;
		}

		if (message.id == WMQ_QUIT)
			break;

		IRP* irp = (IRP*)message.wParam;
		if (!drive_poll_run(drive, irp))
			break;
	}

fail:

	if (error && drive && drive->rdpcontext)
		setChannelError(drive->rdpcontext, error, "drive_thread_func reported an error");

	ExitThread(error);
	return error;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_irp_request(DEVICE* device, IRP* irp)
{
	DRIVE_DEVICE* drive = (DRIVE_DEVICE*)device;

	if (!drive)
		return ERROR_INVALID_PARAMETER;

	if (drive->async)
	{
		if (!MessageQueue_Post(drive->IrpQueue, NULL, 0, (void*)irp, NULL))
		{
			WLog_ERR(TAG, "MessageQueue_Post failed!");
			return ERROR_INTERNAL_ERROR;
		}
	}
	else
	{
		if (!drive_poll_run(drive, irp))
			return ERROR_INTERNAL_ERROR;
	}

	return CHANNEL_RC_OK;
}

static UINT drive_free_int(DRIVE_DEVICE* drive)
{
	UINT error = CHANNEL_RC_OK;

	if (!drive)
		return ERROR_INVALID_PARAMETER;

	(void)CloseHandle(drive->thread);
	ListDictionary_Free(drive->files);
	MessageQueue_Free(drive->IrpQueue);
	Stream_Free(drive->device.data, TRUE);
	free(drive->path);
	free(drive);
	return error;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_free(DEVICE* device)
{
	DRIVE_DEVICE* drive = (DRIVE_DEVICE*)device;
	UINT error = CHANNEL_RC_OK;

	if (!drive)
		return ERROR_INVALID_PARAMETER;

	if (MessageQueue_PostQuit(drive->IrpQueue, 0) &&
	    (WaitForSingleObject(drive->thread, INFINITE) == WAIT_FAILED))
	{
		error = GetLastError();
		WLog_ERR(TAG, "WaitForSingleObject failed with error %" PRIu32 "", error);
		return error;
	}

	return drive_free_int(drive);
}

/**
 * Helper function used for freeing list dictionary value object
 */
static void drive_file_objfree(void* obj)
{
	drive_file_free((DRIVE_FILE*)obj);
}

static void drive_message_free(void* obj)
{
	wMessage* msg = obj;
	if (!msg)
		return;
	if (msg->id != 0)
		return;

	IRP* irp = (IRP*)msg->wParam;
	if (!irp)
		return;
	WINPR_ASSERT(irp->Discard);
	irp->Discard(irp);
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
static UINT drive_register_drive_path(PDEVICE_SERVICE_ENTRY_POINTS pEntryPoints, const char* name,
                                      const char* path, BOOL automount)
{
	size_t length = 0;
	DRIVE_DEVICE* drive = NULL;
	UINT error = ERROR_INTERNAL_ERROR;

	if (!pEntryPoints || !name || !path)
	{
		WLog_ERR(TAG, "[%s] Invalid parameters: pEntryPoints=%p, name=%p, path=%p", pEntryPoints,
		         name, path);
		return ERROR_INVALID_PARAMETER;
	}

	if (name[0] && path[0])
	{
		size_t pathLength = strnlen(path, MAX_PATH);
		drive = (DRIVE_DEVICE*)calloc(1, sizeof(DRIVE_DEVICE));

		if (!drive)
		{
			WLog_ERR(TAG, "calloc failed!");
			return CHANNEL_RC_NO_MEMORY;
		}

		drive->device.type = RDPDR_DTYP_FILESYSTEM;
		drive->device.IRPRequest = drive_irp_request;
		drive->device.Free = drive_free;
		drive->rdpcontext = pEntryPoints->rdpcontext;
		drive->automount = automount;
		length = strlen(name);
		drive->device.data = Stream_New(NULL, length + 1);

		if (!drive->device.data)
		{
			WLog_ERR(TAG, "Stream_New failed!");
			error = CHANNEL_RC_NO_MEMORY;
			goto out_error;
		}

		for (size_t i = 0; i < length; i++)
		{
			/* Filter 2.2.1.3 Device Announce Header (DEVICE_ANNOUNCE) forbidden symbols */
			switch (name[i])
			{
				case ':':
				case '<':
				case '>':
				case '\"':
				case '/':
				case '\\':
				case '|':
				case ' ':
					Stream_Write_UINT8(drive->device.data, '_');
					break;
				default:
					Stream_Write_UINT8(drive->device.data, (BYTE)name[i]);
					break;
			}
		}
		Stream_Write_UINT8(drive->device.data, '\0');

		drive->device.name = Stream_BufferAs(drive->device.data, char);
		if (!drive->device.name)
			goto out_error;

		if ((pathLength > 1) && (path[pathLength - 1] == '/'))
			pathLength--;

		drive->path = ConvertUtf8NToWCharAlloc(path, pathLength, NULL);
		if (!drive->path)
		{
			error = CHANNEL_RC_NO_MEMORY;
			goto out_error;
		}

		drive->files = ListDictionary_New(TRUE);

		if (!drive->files)
		{
			WLog_ERR(TAG, "ListDictionary_New failed!");
			error = CHANNEL_RC_NO_MEMORY;
			goto out_error;
		}

		ListDictionary_ValueObject(drive->files)->fnObjectFree = drive_file_objfree;
		drive->IrpQueue = MessageQueue_New(NULL);

		if (!drive->IrpQueue)
		{
			WLog_ERR(TAG, "ListDictionary_New failed!");
			error = CHANNEL_RC_NO_MEMORY;
			goto out_error;
		}

		wObject* obj = MessageQueue_Object(drive->IrpQueue);
		WINPR_ASSERT(obj);
		obj->fnObjectFree = drive_message_free;

		if ((error = pEntryPoints->RegisterDevice(pEntryPoints->devman, &drive->device)))
		{
			WLog_ERR(TAG, "RegisterDevice failed with error %" PRIu32 "!", error);
			goto out_error;
		}

		drive->async = !freerdp_settings_get_bool(drive->rdpcontext->settings,
		                                          FreeRDP_SynchronousStaticChannels);
		if (drive->async)
		{
			if (!(drive->thread =
			          CreateThread(NULL, 0, drive_thread_func, drive, CREATE_SUSPENDED, NULL)))
			{
				WLog_ERR(TAG, "CreateThread failed!");
				goto out_error;
			}

			ResumeThread(drive->thread);
		}
	}

	return CHANNEL_RC_OK;
out_error:
	drive_free_int(drive);
	return error;
}

/**
 * Function description
 *
 * @return 0 on success, otherwise a Win32 error code
 */
FREERDP_ENTRY_POINT(
    UINT VCAPITYPE drive_DeviceServiceEntry(PDEVICE_SERVICE_ENTRY_POINTS pEntryPoints))
{
	RDPDR_DRIVE* drive = NULL;
	UINT error = 0;
#ifdef WIN32
	int len;
	char devlist[512], buf[512];
	char* bufdup;
	char* devdup;
#endif

	WINPR_ASSERT(pEntryPoints);

	drive = (RDPDR_DRIVE*)pEntryPoints->device;
	WINPR_ASSERT(drive);

#ifndef WIN32
	if (strcmp(drive->Path, "*") == 0)
	{
		/* all drives */
		free(drive->Path);
		drive->Path = _strdup("/");

		if (!drive->Path)
		{
			WLog_ERR(TAG, "_strdup failed!");
			return CHANNEL_RC_NO_MEMORY;
		}
	}
	else if (strcmp(drive->Path, "%") == 0)
	{
		free(drive->Path);
		drive->Path = GetKnownPath(KNOWN_PATH_HOME);

		if (!drive->Path)
		{
			WLog_ERR(TAG, "_strdup failed!");
			return CHANNEL_RC_NO_MEMORY;
		}
	}

	error =
	    drive_register_drive_path(pEntryPoints, drive->device.Name, drive->Path, drive->automount);
#else
	/* Special case: path[0] == '*' -> export all drives */
	/* Special case: path[0] == '%' -> user home dir */
	if (strcmp(drive->Path, "%") == 0)
	{
		GetEnvironmentVariableA("USERPROFILE", buf, sizeof(buf));
		PathCchAddBackslashA(buf, sizeof(buf));
		free(drive->Path);
		drive->Path = _strdup(buf);

		if (!drive->Path)
		{
			WLog_ERR(TAG, "_strdup failed!");
			return CHANNEL_RC_NO_MEMORY;
		}

		error = drive_register_drive_path(pEntryPoints, drive->device.Name, drive->Path,
		                                  drive->automount);
	}
	else if (strcmp(drive->Path, "*") == 0)
	{
		/* Enumerate all devices: */
		GetLogicalDriveStringsA(sizeof(devlist) - 1, devlist);

		for (size_t i = 0;; i++)
		{
			char* dev = &devlist[i * 4];
			if (!*dev)
				break;
			if (*dev > 'B')
			{
				/* Suppress disk drives A and B to avoid pesty messages */
				len = sprintf_s(buf, sizeof(buf) - 4, "%s", drive->device.Name);
				buf[len] = '_';
				buf[len + 1] = dev[0];
				buf[len + 2] = 0;
				buf[len + 3] = 0;

				if (!(bufdup = _strdup(buf)))
				{
					WLog_ERR(TAG, "_strdup failed!");
					return CHANNEL_RC_NO_MEMORY;
				}

				if (!(devdup = _strdup(dev)))
				{
					WLog_ERR(TAG, "_strdup failed!");
					return CHANNEL_RC_NO_MEMORY;
				}

				if ((error = drive_register_drive_path(pEntryPoints, bufdup, devdup, TRUE)))
				{
					break;
				}
			}
		}
	}
	else
	{
		error = drive_register_drive_path(pEntryPoints, drive->device.Name, drive->Path,
		                                  drive->automount);
	}

#endif
	return error;
}
