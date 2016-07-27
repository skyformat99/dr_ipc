// Interprocess communication. Public Domain. See "unlicense" statement at the end of this file.
// dr_webgen - v0.0 - unreleased
//
// David Reid - mackron@gmail.com

// USAGE
//
// dr_ipc is a single-file library. To use it, do something like the following in one .c file.
//   #define DR_IPC_IMPLEMENTATION
//   #include "dr_ipc.h"
//
// You can then #include this file in other parts of the program as you would with any other header file.
//
//
//
// QUICK NOTES
// - Currently, only pipes have been implemented. Sockets will be coming soon.

#ifndef dr_ipc_h
#define dr_ipc_h

#include <stddef.h> // For size_t

#ifdef __cplusplus
extern "C" {
#endif

// Each primitive type in dr_ipc is opaque because otherwise it would require exposing system headers like windows.h
// to the public section of this file.
typedef void* drpipe;

#define DR_IPC_READ     0x01
#define DR_IPC_WRITE    0x02
#define DR_IPC_NOWAIT   0x04

#define DR_IPC_INFINITE 0xFFFFFFFF

typedef enum
{
    dripc_result_success = 0,
    dripc_result_unknown_error,
    dripc_result_invalid_args,
    dripc_result_name_too_long,
    dripc_result_access_denied,
    dripc_result_timeout
} dripc_result;

dripc_result drpipe_open_named_server(const char* name, unsigned int options, drpipe* pPipeOut);
dripc_result drpipe_open_named_client(const char* name, unsigned int options, drpipe* pPipeOut);
dripc_result drpipe_open_anonymous(drpipe* pPipeRead, drpipe* pPipeWrite);
void drpipe_close(drpipe pipe);

// Waits for a client to connect to the given named server piped. This does not return until a client has connected.
dripc_result drpipe_connect(drpipe pipe);
dripc_result drpipe_wait_named(const char* name, unsigned int timeoutInMilliseconds);

dripc_result drpipe_read(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead);
dripc_result drpipe_read_exact(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead);

dripc_result drpipe_write(drpipe pipe, const void* pData, size_t bytesToWrite, size_t* pBytesWritten);

#ifdef __cplusplus
}
#endif
#endif  // dr_ipc


///////////////////////////////////////////////////////////////////////////////
//
// IMPLEMENTATION
//
///////////////////////////////////////////////////////////////////////////////
#ifdef DR_IPC_IMPLEMENTATION

// Platform Detection
#ifdef _WIN32
#define DR_IPC_WIN32
#include <windows.h>
#else
#define DR_IPC_UNIX
#endif


///////////////////////////////////////////////////////////////////////////////
//
// Win32 Implementation
//
///////////////////////////////////////////////////////////////////////////////
#ifdef DR_IPC_WIN32

#define DR_IPC_WIN32_PIPE_NAME_HEAD         "\\\\.\\pipe\\"
#define DR_IPC_WIN32_PIPE_BUFFER_SIZE       512
#define DR_IPC_WIN32_HANDLE_TO_PIPE(handle) ((drpipe)handle)
#define DR_IPC_PIPE_TO_WIN32_HANDLE(pipe)   ((HANDLE)pipe)

static dripc_result dripc_result_from_win32_error(DWORD dwError)
{
    switch (dwError)
    {
    case ERROR_INVALID_PARAMETER: return dripc_result_invalid_args;
    case ERROR_ACCESS_DENIED:     return dripc_result_access_denied;
    case ERROR_SEM_TIMEOUT:       return dripc_result_timeout;
    default:                      return dripc_result_unknown_error;
    }
}

dripc_result drpipe_open_named_server__win32(const char* name, unsigned int options, drpipe* pPipeOut)
{
    char nameWin32[256] = DR_IPC_WIN32_PIPE_NAME_HEAD;
    if (strcat_s(nameWin32, sizeof(nameWin32), name) != 0) {
        return dripc_result_name_too_long;
    }

    DWORD dwOpenMode = FILE_FLAG_FIRST_PIPE_INSTANCE;
    if (options & DR_IPC_READ) {
        if (options & DR_IPC_WRITE) {
            dwOpenMode |= PIPE_ACCESS_DUPLEX;
        } else {
            dwOpenMode |= PIPE_ACCESS_INBOUND;
        }
    } else {
        if (options & DR_IPC_WRITE) {
            dwOpenMode |= PIPE_ACCESS_OUTBOUND;
        } else {
            return dripc_result_invalid_args;   // Neither read nor write mode was specified.
        }
    }

    DWORD dwPipeMode = PIPE_TYPE_BYTE | PIPE_READMODE_BYTE;
    if (options & DR_IPC_NOWAIT) {
        dwPipeMode |= PIPE_NOWAIT;
    }

    HANDLE hPipeWin32 = CreateNamedPipeA(nameWin32, dwOpenMode, dwPipeMode, PIPE_UNLIMITED_INSTANCES, DR_IPC_WIN32_PIPE_BUFFER_SIZE, DR_IPC_WIN32_PIPE_BUFFER_SIZE, NMPWAIT_USE_DEFAULT_WAIT, NULL);
    if (hPipeWin32 == INVALID_HANDLE_VALUE) {
        return dripc_result_from_win32_error(GetLastError());
    }

    *pPipeOut = DR_IPC_WIN32_HANDLE_TO_PIPE(hPipeWin32);
    return dripc_result_success;
}

dripc_result drpipe_open_named_client__win32(const char* name, unsigned int options, drpipe* pPipeOut)
{
    char nameWin32[256] = DR_IPC_WIN32_PIPE_NAME_HEAD;
    if (strcat_s(nameWin32, sizeof(nameWin32), name) != 0) {
        return dripc_result_name_too_long;
    }

    DWORD dwDesiredAccess = 0;
    if (options & DR_IPC_READ) {
        dwDesiredAccess |= GENERIC_READ;
    }
    if (options & DR_IPC_WRITE) {
        dwDesiredAccess |= GENERIC_WRITE;
    }

    if (dwDesiredAccess == 0) {
        return dripc_result_invalid_args;   // Neither read nor write mode was specified.
    }

    // The pipe might be busy, so just keep trying.
    for (;;) {
        HANDLE hPipeWin32 = CreateFileA(nameWin32, dwDesiredAccess, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipeWin32 == INVALID_HANDLE_VALUE) {
            DWORD dwError = GetLastError();
            if (dwError != ERROR_PIPE_BUSY) {
                return dripc_result_from_win32_error(dwError);
            }
        } else {
            *pPipeOut = DR_IPC_WIN32_HANDLE_TO_PIPE(hPipeWin32);
            break;
        }
    }

    return dripc_result_success;
}

dripc_result drpipe_open_anonymous__win32(drpipe* pPipeRead, drpipe* pPipeWrite)
{
    HANDLE hPipeReadWin32;
    HANDLE hPipeWriteWin32;
    if (!CreatePipe(&hPipeReadWin32, &hPipeWriteWin32, NULL, DR_IPC_WIN32_PIPE_BUFFER_SIZE)) {
        return dripc_result_from_win32_error(GetLastError());
    }

    *pPipeRead = DR_IPC_WIN32_HANDLE_TO_PIPE(hPipeReadWin32);
    *pPipeWrite = DR_IPC_WIN32_HANDLE_TO_PIPE(hPipeWriteWin32);
    return dripc_result_success;
}

void drpipe_close__win32(drpipe pipe)
{
    CloseHandle(DR_IPC_PIPE_TO_WIN32_HANDLE(pipe));
}


dripc_result drpipe_connect__win32(drpipe pipe)
{
    if (!ConnectNamedPipe(pipe, NULL)) {
        return dripc_result_from_win32_error(GetLastError());
    }

    return dripc_result_success;
}

dripc_result drpipe_wait_named__win32(const char* name, unsigned int timeoutInMilliseconds)
{
    char nameWin32[256] = DR_IPC_WIN32_PIPE_NAME_HEAD;
    if (strcat_s(nameWin32, sizeof(nameWin32), name) != 0) {
        return dripc_result_name_too_long;
    }

    if (!WaitNamedPipeA(nameWin32, timeoutInMilliseconds)) {
        return dripc_result_from_win32_error(GetLastError());
    }

    return dripc_result_success;
}


dripc_result drpipe_read__win32(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead)
{
    HANDLE hPipe = DR_IPC_PIPE_TO_WIN32_HANDLE(pipe);

    DWORD dwBytesRead;
    if (!ReadFile(hPipe, pDataOut, bytesToRead, &dwBytesRead, NULL)) {
        return dripc_result_from_win32_error(GetLastError());
    }

    *pBytesRead = dwBytesRead;
    return dripc_result_success;
}

dripc_result drpipe_write__win32(drpipe pipe, const void* pData, size_t bytesToWrite, size_t* pBytesWritten)
{
    HANDLE hPipe = DR_IPC_PIPE_TO_WIN32_HANDLE(pipe);

    DWORD dwBytesWritten;
    if (!WriteFile(hPipe, pData, bytesToWrite, &dwBytesWritten, NULL)) {
        return dripc_result_from_win32_error(GetLastError());
    }

    *pBytesWritten = dwBytesWritten;
    return dripc_result_success;
}
#endif  // Win32


///////////////////////////////////////////////////////////////////////////////
//
// Unix Implementation
//
///////////////////////////////////////////////////////////////////////////////
#ifdef DR_IPC_UNIX

#define DR_IPC_UNIX_PIPE_NAME_HEAD  "/tmp/"
#define DR_IPC_UNIX_SERVER          (1 << 31)
#define DR_IPC_UNIX_CLIENT          (1 << 30)

typedef struct
{
    int fd;
    unsigned int options;
    char name[1];
} drpipe_unix;

static dripc_result dripc_result_from_unix_error(int error)
{
    switch (error)
    {
    default: return dripc_result_unknown_error;
    }
}

static int dripc_options_to_fd_open_flags(unsigned int options)
{
    int flags = 0;
    if (options & DR_IPC_READ) {
        if (options & DR_IPC_WRITE) {
            flags |= O_RDWR;
        } else {
            flags |= O_RDONLY;
        }
    } else {
        if (options & DR_IPC_WRITE) {
            flags |= O_WRONLY;
        } else {
            return dripc_result_invalid_args;   // Neither read nor write mode was specified.
        }
    }

    if (options & DR_IPC_NOWAIT) {
        flags |= O_NONBLOCK;
    }

    return flags;
}

dripc_result drpipe_open_named_server__unix(const char* name, unsigned int options, drpipe* pPipeOut)
{
    char nameUnix[256] = DR_IPC_UNIX_PIPE_NAME_HEAD;
    if (strcat_s(nameUnix, sizeof(nameUnix), name) != 0) {
        return dripc_result_name_too_long;
    }

    if (mkfifo(nameUnix, 0666) == -1) {
        return dripc_result_from_unix_error(errno);
    }


    drpipe_unix* pPipeUnix = (drpipe_unix*)malloc(sizeof(*pPipeUnix) + strlen(nameUnix)+1);     // +1 for null terminator.
    if (pPipeUnix == NULL) {
        return dripc_result_unknown_error;
    }

    pPipeUnix->fd = -1;
    pPipeUnix->options = options | DR_IPC_UNIX_SERVER;
    strcpy(pPipeUnix->name, nameUnix);

    *pPipeOut = (drpipe)pPipeUnix;
    return dripc_result_success;
}

dripc_result drpipe_open_named_client__unix(const char* name, unsigned int options, drpipe* pPipeOut)
{
    char nameUnix[256] = DR_IPC_UNIX_PIPE_NAME_HEAD;
    if (strcat_s(nameUnix, sizeof(nameUnix), name) != 0) {
        return dripc_result_name_too_long;
    }

    drpipe_unix* pPipeUnix = (drpipe_unix*)malloc(sizeof(*pPipeUnix) + strlen(nameUnix)+1);     // +1 for null terminator.
    if (pPipeUnix == NULL) {
        return dripc_result_unknown_error;
    }

    pPipeUnix->options = options | DR_IPC_UNIX_CLIENT;
    strcpy(pPipeUnix->name, nameUnix);

    pPipeUnix->fd = open(nameUnix, dripc_options_to_fd_open_flags(options));
    if (pPipeUnix->fd == -1) {
        free(pPipeUnix);
        return dripc_result_from_unix_error(errno);
    }


    *pPipeOut = (drpipe)pPipeUnix;
    return dripc_result_success;
}

dripc_result drpipe_open_anonymous__unix(drpipe* pPipeRead, drpipe* pPipeWrite)
{
    drpipe_unix* pPipeReadUnix = (drpipe_unix*)calloc(1, sizeof(*pPipeReadUnix) + 1);
    if (pPipeReadUnix == NULL) {
        return dripc_result_unknown_error;
    }

    drpipe_unix* pPipeWriteUnix = (drpipe_unix*)calloc(1, sizeof(*pPipeWriteUnix) + 1);
    if (pPipeWriteUnix == NULL) {
        free(pPipeReadUnix);
        return dripc_result_unknown_error;
    }

    int pipeFDs[2];
    if (pipe(pipeFDs) == -1) {
        free(pPipeWriteUnix);
        free(pPipeReadUnix);
        return dripc_result_from_unix_error(errno);
    }

    pPipeReadUnix->fd  = pipeFDs[0];
    pPipeWriteUnix->fd = pipeFDs[1];

    *pPipeRead = pPipeReadUnix;
    *pPipeWrite = pPipeWriteUnix;

    return dripc_result_success;
}

void drpipe_close__unix(drpipe pipe)
{
    drpipe_unix* pPipeUnix = (drpipe_unix*)pipe;

    if (pPipeUnix->fd != -1) {
        close(pPipeUnix->fd);
    }

    if (pPipeUnix->options & DR_IPC_UNIX_SERVER) {
        unlink(pPipeUnix->name);
    }
}


dripc_result drpipe_connect__unix(drpipe pipe)
{
    // Here is where we actually open the file. This should block until a client connects.
    drpipe_unix* pPipeUnix = (drpipe_unix*)pipe;
    if (pPipeUnix->fd != -1) {
        return dripc_result_unknown_error;  // Alread have a connection.
    }

    pPipeUnix->fd = open(pPipeUnix->name, dripc_options_to_fd_open_flags(pPipeUnix->options));
    if (pPipeUnix->fd == -1) {
        return dripc_result_from_unix_error(errno);
    }

    return dripc_result_success;
}

dripc_result drpipe_wait_named__unix(const char* name, unsigned int timeoutInMilliseconds)
{
    (void)name;
    (void)timeoutInMilliseconds;
    return dripc_result_success;
}


dripc_result drpipe_read__unix(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead)
{
    drpipe_unix* pPipeUnix = (drpipe_unix*)pipe;

    ssize_t bytesRead = read(pPipeUnix->fd, pDataOut, bytesToRead);
    if (bytesRead == -1) {
        return dripc_result_from_unix_error(errno);
    }

    *pBytesRead = (size_t)bytesRead;
    return dripc_result_success;
}

dripc_result drpipe_write__unix(drpipe pipe, const void* pData, size_t bytesToWrite, size_t* pBytesWritten)
{
    drpipe_unix* pPipeUnix = (drpipe_unix*)pipe;

    ssize_t bytesWritten = write(pPipeUnix->fd, pData, bytesToWrite);
    if (bytesWritten == -1) {
        return dripc_result_from_unix_error(errno);
    }

    *pBytesWritten = (size_t)bytesWritten;
    return dripc_result_success;
}
#endif  // Unix

dripc_result drpipe_open_named_server(const char* name, unsigned int options, drpipe* pPipeOut)
{
    if (name == NULL || options == 0 || pPipeOut == NULL) {
        return dripc_result_invalid_args;
    }

    *pPipeOut = NULL;


#ifdef DR_IPC_WIN32
    return drpipe_open_named_server__win32(name, options, pPipeOut);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_open_named_server__unix(name, options, pPipeOut);
#endif
}

dripc_result drpipe_open_named_client(const char* name, unsigned int options, drpipe* pPipeOut)
{
    if (name == NULL || options == 0 || pPipeOut == NULL) {
        return dripc_result_invalid_args;
    }

    *pPipeOut = NULL;


#ifdef DR_IPC_WIN32
    return drpipe_open_named_client__win32(name, options, pPipeOut);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_open_named_client__unix(name, options, pPipeOut);
#endif
}

dripc_result drpipe_open_anonymous(drpipe* pPipeRead, drpipe* pPipeWrite)
{
    if (pPipeRead == NULL || pPipeWrite == NULL) {
        return dripc_result_invalid_args;
    }

    *pPipeRead = NULL;
    *pPipeWrite = NULL;


#ifdef DR_IPC_WIN32
    return drpipe_open_anonymous__win32(pPipeRead, pPipeWrite);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_open_anonymous__unix(pPipeRead, pPipeWrite);
#endif
}

void drpipe_close(drpipe pipe)
{
    if (pipe == NULL) {
        return;
    }

#ifdef DR_IPC_WIN32
    drpipe_close__win32(pipe);
#endif

#ifdef DR_IPC_UNIX
    drpipe_close__unix(pipe);
#endif
}


dripc_result drpipe_connect(drpipe pipe)
{
#ifdef DR_IPC_WIN32
    return drpipe_connect__win32(pipe);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_connect__unix(pipe);
#endif
}

dripc_result drpipe_wait_named(const char* name, unsigned int timeoutInMilliseconds)
{
    if (name == NULL) {
        return dripc_result_invalid_args;
    }

#ifdef DR_IPC_WIN32
    return drpipe_wait_named__win32(name, timeoutInMilliseconds);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_wait_named__unix(name, timeoutInMilliseconds);
#endif
}


dripc_result drpipe_read(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead)
{
    if (pBytesRead) *pBytesRead = 0;

    if (pipe == NULL || pDataOut == NULL) {
        return dripc_result_invalid_args;
    }

    // Currently, reading is restricted to 2^31 bytes.
    if (bytesToRead > 0x7FFFFFFF) {
        return dripc_result_invalid_args;
    }


#ifdef DR_IPC_WIN32
    return drpipe_read__win32(pipe, pDataOut, bytesToRead, pBytesRead);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_read__unix(pipe, pDataOut, bytesToRead, pBytesRead);
#endif
}

dripc_result drpipe_read_exact(drpipe pipe, void* pDataOut, size_t bytesToRead, size_t* pBytesRead)
{
    if (pBytesRead) *pBytesRead = 0;

    while (bytesToRead > 0) {
        size_t bytesRead;
        dripc_result result = drpipe_read(pipe, pDataOut, (bytesToRead <= 0x7FFFFFFF) ? bytesToRead : 0x7FFFFFFF, &bytesRead);
        if (result != dripc_result_success) {
            return result;
        }

        pDataOut = (void*)((char*)pDataOut + bytesRead);

        bytesToRead -= bytesRead;
        if (pBytesRead) *pBytesRead += bytesRead;
    }

    return dripc_result_success;
}


dripc_result drpipe_write(drpipe pipe, const void* pData, size_t bytesToWrite, size_t* pBytesWritten)
{
    if (pBytesWritten) *pBytesWritten = 0;

    if (pipe == NULL || pData == NULL) {
        return dripc_result_invalid_args;
    }

    // Currently, writing is restricted to 2^31 bytes.
    if (bytesToWrite > 0x7FFFFFFF) {
        return dripc_result_invalid_args;
    }


#ifdef DR_IPC_WIN32
    return drpipe_write__win32(pipe, pData, bytesToWrite, pBytesWritten);
#endif

#ifdef DR_IPC_UNIX
    return drpipe_write__unix(pipe, pData, bytesToWrite, pBytesWritten);
#endif
}

#endif  // DR_IPC_IMPLEMENTATION


/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>
*/
