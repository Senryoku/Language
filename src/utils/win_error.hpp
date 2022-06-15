#pragma once

#ifdef WIN32
void win_error_exit(const char* lpszFunction) {
    error("Error: {}\n", lpszFunction);

    // Retrieve the system error message for the last-error code

    char*         lpMsgBuf = nullptr;
    STRSAFE_LPSTR lpDisplayBuf = nullptr;
    DWORD         dw = GetLastError();

    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                  (LPTSTR)&lpMsgBuf, 0, NULL);

    // Display the error message and exit the process

    lpDisplayBuf = (STRSAFE_LPSTR)LocalAlloc(LMEM_ZEROINIT, (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));
    if(lpDisplayBuf) {
        StringCchPrintf((LPTSTR)lpDisplayBuf, LocalSize(lpDisplayBuf) / sizeof(TCHAR), TEXT("%s failed with error %d: %s"), lpszFunction, dw, lpMsgBuf);
        MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);
    }

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(dw);
}
#endif
