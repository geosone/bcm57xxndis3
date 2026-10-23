/* Prove that Win16 starts, then return cleanly to DOS after three seconds. */
#include <windows.h>

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR args, int show)
{
    MSG message;
    HFILE marker;
    (void)instance; (void)previous; (void)args; (void)show;
    marker = _lcreat("C:\\BCMDONE.TXT", 0);
    if (marker != HFILE_ERROR) {
        _lwrite(marker, "Win16 application started\r\n", 27);
        _lclose(marker);
    }
    if (!SetTimer(NULL, 1, 3000, NULL))
        return 1;
    while (GetMessage(&message, NULL, 0, 0)) {
        if (message.message == WM_TIMER)
            ExitWindows(0, 0);
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    return 0;
}
