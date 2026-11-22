; IRQSRC.COM - which PCI function asserts IRQ 11?  Turns INTx off (command
; bit 10) on every function routed to it, then on again one at a time.
        cpu 386
        org 100h
        mov ax, 3573h
        int 21h
        mov [old73], bx
        mov [old73+2], es
        push cs
        pop es
        mov dx, handler
        mov ax, 2573h
        int 21h
        mov si, m_start
        call puts
        call irr
        call hex2
        call crlf
        mov byte [pass], 0      ; pass 0: all off
        call walk
        mov si, m_alloff
        call puts
        call count_window
        call hex4
        mov ax, [stuck]
        call hex4
        call crlf
        mov si, m_alloff
        call puts
        call count_window
        call hex4
        call crlf
        mov byte [pass], 1      ; pass 1: one on, count, off again
        call walk
        mov byte [pass], 2      ; pass 2: restore
        call walk
        mov si, m_end
        call puts
        call irr
        call hex2
        call crlf
        push ds
        lds dx, [old73]
        mov ax, 2573h
        int 21h
        pop ds
        mov ax, 4C00h
        int 21h

; unmask IRQ 11 for a while; AX = interrupts taken (stops at 1000)
count_window:
        mov word [count], 0
        cli
        in  al, 0A1h
        and al, 0F7h
        out 0A1h, al
        sti
        call delay
        call delay
        cli
        in  al, 0A1h
        or  al, 08h
        out 0A1h, al
        sti
        mov ax, [count]
        ret

handler:
        push ax
        inc word [cs:count]
        cmp word [cs:count], 1000
        jb  .eoi
        in  al, 0A1h
        or  al, 08h
        out 0A1h, al
.eoi:   mov al, 20h
        out 0A0h, al
        out 20h, al
        pop ax
        iret

; config word read/write through ports CF8h/CFCh; BX = bus/devfn, DI = offset
cfg_addr:
        push eax
        push dx
        xor eax, eax
        mov ax, [cur]
        shl eax, 8
        or  al, [cfgoff]
        and al, 0FCh
        or  eax, 80000000h
        mov dx, 0CF8h
        out dx, eax
        pop dx
        pop eax
        ret
rcfg:   mov [cfgoff], di        ; -> CX
        call cfg_addr
        mov dx, di
        and dx, 3
        add dx, 0CFCh
        in  ax, dx
        mov cx, ax
        ret
wcmd:   mov word [cfgoff], 4    ; CX -> command register
        call cfg_addr
        mov dx, 0CFCh
        mov ax, cx
        out dx, ax
        ret

walk:   xor bx, bx              ; BH = bus, BL = dev<<3 | fn
.next:  mov [cur], bx
        xor di, di
        call rcfg
        cmp cx, 0FFFFh
        je  .skip
        mov di, 3Ch             ; line (CL), pin (CH)
        call rcfg
        or  ch, ch
        jz  .skip               ; no interrupt pin
        mov [line], cx
        mov di, 4
        call rcfg               ; CX = command
        cmp byte [pass], 0
        jne .p1
        mov bx, [cur]
        cmp byte [first], 0
        jne .n0
        mov [orig], cx
.n0:    or  cx, 0400h
        call wcmd
        mov di, 4
        call rcfg
        test cx, 0400h
        jnz .skip
        inc word [stuck]        ; INTx disable does not stick
        mov ax, [cur]
        call hex4
        mov ax, [line]
        call hex4
        mov si, m_stuck
        call puts
        jmp .skip
.p1:    cmp byte [pass], 1
        jne .p2
        and cx, 0FBFFh
        call wcmd
        call count_window
        push ax
        mov ax, [cur]
        call hex4
        mov ax, [line]
        call hex4
        mov di, 6
        call rcfg
        mov ax, cx
        call hex4
        pop ax
        call hex4
        call crlf
        mov di, 4
        call rcfg
        or  cx, 0400h
        call wcmd
        jmp .skip
.p2:    and cx, 0FBFFh
        call wcmd
.skip:  mov bx, [cur]
        inc bx
        cmp bh, 10h
        jb  .next
        ret
delay:  push cx
        mov cx, 0
.d:     in  al, 80h
        loop .d
        pop cx
        ret
irr:    cli
        mov al, 0Ah
        out 0A0h, al
        in  al, 0A0h
        sti
        ret
hex4:   push ax
        mov al, ah
        call hex2n
        pop ax
hex2:   call hex2n
        push ax
        mov dl, ' '
        mov ah, 2
        int 21h
        pop ax
        ret
hex2n:  push ax
        push ax
        shr al, 4
        call nib
        pop ax
        call nib
        pop ax
        ret
nib:    and al, 0Fh
        add al, '0'
        cmp al, '9'
        jbe .o
        add al, 7
.o:     mov dl, al
        mov ah, 2
        int 21h
        ret
crlf:   mov dl, 13
        mov ah, 2
        int 21h
        mov dl, 10
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
m_start:  db 'slave IRR at start: ', 0
m_stuck: db 'keeps INTx on', 13, 10, 0
m_alloff: db 'all INTx off (direct, read back), interrupts taken / not sticking: ', 0
m_end:    db 'restored, IRR: ', 0
pass:   db 0
first:  db 0
cfgoff: dw 0
line:   dw 0
orig:   dw 0
stuck:  dw 0
count:  dw 0
old73:  dw 0, 0
cur:    dw 0
