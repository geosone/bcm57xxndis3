; PROMK.COM - Promise PDC20268 (105a:4d68): raise and acknowledge one real
; drive interrupt per channel (IDENTIFY PACKET DEVICE on the master with
; interrupts enabled, data read, status read).  Diagnostic: does a fresh
; assert/deassert cycle free an INTx line a PCIe-to-PCI bridge holds?
        cpu 386
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
        ret
.found: mov si, m_k
        call puts
        mov cx, 2
        mov di, 10h
.chan:  push cx
        call rcfg32
        and ax, 0FFFCh
        mov [cmd], ax
        add di, 4
        call rcfg32
        and ax, 0FFFCh
        add ax, 2
        mov [ctl], ax
        add di, 4
        push di
        mov dx, [ctl]
        mov al, 08h             ; interrupts on
        out dx, al
        mov dx, [cmd]
        add dx, 6
        mov al, 0A0h
        out dx, al
        call wait1
        mov dx, [cmd]
        add dx, 7
        mov al, 0A1h            ; IDENTIFY PACKET DEVICE
        out dx, al
        call wait1
        mov cx, 0FFFFh
.busy:  mov dx, [ctl]
        in  al, dx
        test al, 80h
        jz  .ready
        push cx
        call wait1
        pop cx
        loop .busy
.ready: call hex2               ; alternate status
        mov dx, [ctl]
        in  al, dx
        test al, 08h
        jz  .nodrq
        mov dx, [cmd]
        mov cx, 256
.data:  in  ax, dx
        loop .data
.nodrq: call wait1
        mov dx, [cmd]
        add dx, 7
        in  al, dx              ; status: interrupt acknowledged
        call hex2
        pop di
        pop cx
        loop .chan
        mov si, m_crlf
        call puts
        ret
wait1:  mov cx, 8
.w:     mov dx, [ctl]
        in  al, dx
        loop .w
        ret
rcfg32: xor eax, eax
        mov ax, [cur]
        shl eax, 8
        or  ax, di
        or  eax, 80000000h
        mov dx, 0CF8h
        out dx, eax
        mov dx, 0CFCh
        in  eax, dx
        ret
hex2:   push ax
        shr al, 4
        call nib
        pop ax
        call nib
        mov dl, ' '
        mov ah, 2
        int 21h
        ret
nib:    push ax
        and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
        mov ah, 2
        int 21h
        pop ax
        ret
puts:   lodsb
        or  al, al
        jz  .e
        mov dl, al
        mov ah, 2
        int 21h
        jmp puts
.e:     ret
m_k:    db 'PROMK: alt status, status per channel: ', 0
m_crlf: db 13, 10, 0
cur:    dw 0
cmd:    dw 0
ctl:    dw 0
