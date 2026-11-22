; IRQCLR.COM - take a request that is latched on the masked PCI IRQ 11
; once, so that a Windows driver that unmasks the line later does not start
; with it.  Prints what it found.
        org 100h
        mov ax, 3573h
        int 21h
        mov [old], bx
        mov [old+2], es
        mov dx, handler
        mov ax, 2573h
        int 21h
        call irr
        mov [before], al
        cli
        in  al, 0A1h
        mov [mask], al
        and al, 0F7h
        out 0A1h, al
        sti
        mov cx, 0
.w:     in  al, 80h
        loop .w
        cli
        in  al, 0A1h
        and al, 0F7h
        mov ah, [mask]
        and ah, 08h
        or  al, ah
        out 0A1h, al
        sti
        push ds
        lds dx, [old]
        mov ax, 2573h
        int 21h
        pop ds
        mov si, m1
        call puts
        mov al, [before]
        shr al, 3
        and al, 1
        add al, '0'
        call putc
        mov si, m2
        call puts
        mov ax, [count]
        call dec
        mov si, m3
        call puts
        call irr
        shr al, 3
        and al, 1
        add al, '0'
        call putc
        mov si, m4
        call puts
        mov ax, 4C00h
        int 21h
handler:
        push ax
        inc word [cs:count]
        cmp word [cs:count], 1000
        jb  .e
        in  al, 0A1h
        or  al, 08h
        out 0A1h, al
        or  byte [cs:mask], 08h
.e:     mov al, 63h
        out 0A0h, al
        mov al, 62h
        out 20h, al
        pop ax
        iret
irr:    cli
        mov al, 0Ah
        out 0A0h, al
        in  al, 0A0h
        sti
        ret
dec:    xor cx, cx
        mov bx, 10
.d:     xor dx, dx
        div bx
        push dx
        inc cx
        or  ax, ax
        jnz .d
.p:     pop ax
        add al, '0'
        call putc
        loop .p
        ret
putc:   mov dl, al
        mov ah, 2
        int 21h
        ret
puts:   lodsb
        or  al, al
        jz  .x
        call putc
        jmp puts
.x:     ret
m1:     db 'IRQCLR: IRQ 11 requested before=', 0
m2:     db ', interrupts taken=', 0
m3:     db ', requested after=', 0
m4:     db 13, 10, 0
before: db 0
mask:   db 0
count:  dw 0
old:    dw 0, 0
