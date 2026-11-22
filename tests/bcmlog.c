/* BCMLOG [x]: writes the driver's log to C:\BCMLOG.TXT through the VxD's
 * protected mode API (device ID 3A57h); with "x" it then ends Windows, so
 * that the file is on disk. */
#include <windows.h>

static void (__far *api)(void);

static unsigned api_entry(void)
{
    unsigned found = 0;
    __asm {
        push es
        push di
        xor di, di
        mov es, di
        mov ax, 1684h
        mov bx, 3a57h
        int 2fh
        mov word ptr api, di
        mov word ptr api+2, es
        mov ax, es
        or ax, di
        mov found, ax
        pop di
        pop es
    }
    return found;
}

static unsigned log_char(unsigned index)
{
    unsigned value;
    __asm {
        mov ax, index
        call dword ptr api
        mov value, ax
    }
    return value;
}

int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR args, int show)
{
    HFILE file;
    unsigned index, value;
    char c;
    (void)instance; (void)previous; (void)show;
    file = _lcreat("C:\\BCMLOG.TXT", 0);
    if (file == HFILE_ERROR)
        return 1;
    if (!api_entry())
        _lwrite(file, "BCM57XX is not loaded\r\n", 24);
    else
        for (index = 0; index < 6000; index++) {
            value = log_char(index);
            if (value == 0xffff)
                break;
            c = (char)value;
            _lwrite(file, &c, 1);
        }
    _lclose(file);
    if (*args == 'x' || *args == 'X')
        ExitWindows(0, 0);
    return 0;
}
