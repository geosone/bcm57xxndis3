; PROMQ.COM - quiet a Promise PDC20268 (105a:4d68): print its I/O BARs,
; read both channels' status (acknowledges a pending drive interrupt) and
; set nIEN in the device control registers.  Diagnostic for a level
; interrupt held on a shared line by a controller its driver only polls.
        cpu 386
%ifndef NIENVAL
%define NIENVAL 0Ah
%endif
        org 100h
        xor bx, bx
.find:  mov [cur], bx
        xor di, di
        call rcfg32
        cmp eax, 4D68105Ah
        je  .found
        inc bx
        cmp bh, 10h
        jb  .find
        mov si, m_none
        call puts
        ret
.found: mov si, m_at
        call puts
        mov ax, [cur]
        call hex4
        call crlf
        mov di, 20h             ; bus master block: dump 16 bytes
        call rcfg32
        and ax, 0FFFCh
        mov dx, ax
        mov cx, 16
.bm:    in  al, dx
        push dx
        push cx
        call hex2
        pop cx
        pop dx
        inc dx
        loop .bm
        call crlf
        mov cx, 2               ; two channels: BAR0/1, BAR2/3
        mov di, 10h
.chan:  push cx
        call rcfg32
        and ax, 0FFFCh
        mov [cmd], ax
        add di, 4
        call rcfg32
        and ax, 0FFFCh
        mov [ctl], ax
        add di, 4
        push di
        mov ax, [cmd]
        call hex4
        mov ax, [ctl]
        call hex4
        mov bl, 0A0h
.drv:   mov dx, [cmd]
        add dx, 6
        mov al, bl
        out dx, al
        call wait1
        mov dx, [ctl]
        add dx, 2
        in  al, dx              ; alternate status (no acknowledge)
        call hex2
%ifndef NOSTATUS
        mov dx, [cmd]
        add dx, 7
        in  al, dx              ; status: acknowledges the interrupt
        call hex2
%endif
%ifndef NONIEN
        mov dx, [ctl]
        add dx, 2
        mov al, NIENVAL
        out dx, al
%endif
        add bl, 10h
        cmp bl, 0B0h
        jbe .drv
        mov dx, [cmd]
        add dx, 6
        mov al, 0A0h
        out dx, al
        call crlf
        pop di
        pop cx
        loop .chan
        ret

wait1:  mov cx, 4
.w:     mov dx, [ctl]
        add dx, 2
        in  al, dx
        loop .w
        ret
rcfg32: xor eax, eax            ; DI = offset -> EAX
        mov ax, [cur]
        shl eax, 8
        or  ax, di
        or  eax, 80000000h
        mov dx, 0CF8h
        out dx, eax
        mov dx, 0CFCh
        in  eax, dx
        ret
hex4:   push ax
        mov al, ah
        call hex2n
        pop ax
hex2:   call hex2n
        mov dl, ' '
        mov ah, 2
        int 21h
        ret
hex2n:  push ax
        shr al, 4
        call nib
        pop ax
nib:    and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
        mov ah, 2
        int 21h
        ret
crlf:   mov si, m_crlf
puts:   lodsb
        or  al, al
        jz  .e
        mov dl, al
        mov ah, 2
        int 21h
        jmp puts
.e:     ret
m_none: db 'PROMQ: no 105a:4d68', 13, 10, 0
m_at:   db 'PROMQ: 105a:4d68 at ', 0
m_crlf: db 13, 10, 0
cur:    dw 0
cmd:    dw 0
ctl:    dw 0
