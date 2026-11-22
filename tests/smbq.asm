; SMBQ.COM - Intel PCH SMBus controller (bus 0, device 1Fh, function 4):
; print host status/control and slave status/command; unless built with
; -DVIEW, switch its interrupt sources off and clear the pending status.
; Diagnostic for a level interrupt held on a shared PCI line.
        cpu 386
        org 100h
        mov eax, 8000FC00h
        mov dx, 0CF8h
        out dx, eax
        mov dx, 0CFCh
        in  eax, dx
        mov [id], eax
        mov eax, 8000FC20h
        mov dx, 0CF8h
        out dx, eax
        mov dx, 0CFCh
        in  eax, dx
        and ax, 0FFE0h
        mov [base], ax
        mov si, m_id
        call puts
        mov ax, [id+2]
        call hex4
        mov ax, [id]
        call hex4
        mov ax, [base]
        call hex4
        call show
%ifndef VIEW
        mov dx, [base]
        add dx, 11h             ; slave command: host notify / alert off
        xor al, al
        out dx, al
        mov dx, [base]
        add dx, 2               ; host control: INTREN off
        in  al, dx
        and al, 0FEh
        out dx, al
        mov dx, [base]
        add dx, 10h             ; slave status: write 1 to clear
        mov al, 0FFh
        out dx, al
        mov dx, [base]          ; host status: write 1 to clear
        mov al, 0FFh
        out dx, al
        mov si, m_after
        call puts
        call show
%endif
        ret
show:   mov dx, [base]
        in  al, dx
        call hex2
        mov dx, [base]
        add dx, 2
        in  al, dx
        call hex2
        mov dx, [base]
        add dx, 10h
        in  al, dx
        call hex2
        mov dx, [base]
        add dx, 11h
        in  al, dx
        call hex2
        mov si, m_crlf
        jmp puts
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
puts:   lodsb
        or  al, al
        jz  .e
        mov dl, al
        mov ah, 2
        int 21h
        jmp puts
.e:     ret
m_id:    db 'SMBQ: id, base, host status/control, slave status/command: ', 0
m_after: db 'SMBQ: after: ', 0
m_crlf:  db 13, 10, 0
id:      dd 0
base:    dw 0
