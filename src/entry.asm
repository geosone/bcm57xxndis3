; Minimal static Windows 3.x VxD, using the documented 56-byte DDB layout.
; NdisGetVersion: device 0028h, service ordinal 0 in NET/INC/NDIS.INC.
; No Microsoft DDK macros or precompiled NDIS libraries are required.
    .386p
    .model flat, c
    option casemap:none

bcm_probe_init proto c
bcm_mac_stop proto c

    .code

control_proc proc
    cmp eax, 1                  ; Device_Init
    jne control_exit
    pushad
    cld
    call bcm_probe_init
    popad
    jmp control_done
control_exit:
    cmp eax, 5                  ; System_Exit, while NDIS is still available
    jne control_done
    pushad
    cld
    call bcm_mac_stop
    popad
control_done:
    clc
    ret
control_proc endp

bcm_vmm_version proc
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    xor eax, eax
    int 20h
    dd 00010000h                ; Get_VMM_Version
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    cld
    ret
bcm_vmm_version endp

bcm_ndis_version proc
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    xor eax, eax                ; GetVersion may OR its version into EAX
    int 20h
    dd 00280000h                ; NdisGetVersion
    jnc version_done
    xor eax, eax
version_done:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    cld
    ret
bcm_ndis_version endp

; WfW NDIS 3.0 services use stack arguments and cdecl cleanup. These
; thunks deliberately do not use the Win95 stdcall LIBNDIS library.
bcm_ndis_initialize proc result:dword, ids:dword, path:dword, specific:dword
    pushad
    push specific
    push path
    push ids
    push result
    int 20h
    dd 00280031h
    add esp, 16
    popad
    cld
    ret
bcm_ndis_initialize endp

bcm_ndis_register proc status:dword, result:dword, wrapper:dword, \
                       context:dword, characteristics:dword, char_size:dword
    pushad
    push char_size
    push characteristics
    push context
    push wrapper
    push result
    push status
    int 20h
    dd 00280033h
    add esp, 24
    popad
    cld
    ret
bcm_ndis_register endp

bcm_ndis_deregister proc status:dword, handle:dword
    pushad
    push handle
    push status
    int 20h
    dd 00280034h
    add esp, 8
    popad
    cld
    ret
bcm_ndis_deregister endp

bcm_ndis_terminate proc handle:dword, specific:dword
    pushad
    push specific
    push handle
    int 20h
    dd 00280032h
    add esp, 8
    popad
    cld
    ret
bcm_ndis_terminate endp

; Locked mapping for the VxD lifetime. WfW tears it down on Windows exit.
bcm_map_mmio proc physical:dword
    push ebx
    push esi
    push edi
    push 0
    push 10000h                 ; BCM5722 BAR0 register/window aperture
    push physical
    int 20h
    dd 0001006ch                ; _MapPhysToLinear
    add esp, 12
    pop edi
    pop esi
    pop ebx
    cld
    ret
bcm_map_mmio endp

; A complete CF8/CFC read is atomic with respect to maskable interrupts.
; Restore both the address latch and the caller's interrupt state.
bcm_config_read proc address:dword
    pushfd
    cli
    push edx
    push ecx
    mov dx, 0cf8h
    in eax, dx
    mov ecx, eax
    mov eax, address
    out dx, eax
    mov dx, 0cfch
    in eax, dx
    push eax
    mov eax, ecx
    mov dx, 0cf8h
    out dx, eax
    pop eax
    pop ecx
    pop edx
    popfd
    ret
bcm_config_read endp

; 16-bit read at 0CFCh + (address & 2); the latch gets the dword address.
bcm_config_read16 proc address:dword
    pushfd
    cli
    push edx
    push ecx
    mov dx, 0cf8h
    in eax, dx
    mov ecx, eax
    mov eax, address
    and al, 0fch
    out dx, eax
    mov edx, address
    and edx, 2
    add dx, 0cfch
    in ax, dx
    movzx eax, ax
    push eax
    mov eax, ecx
    mov dx, 0cf8h
    out dx, eax
    pop eax
    pop ecx
    pop edx
    popfd
    ret
bcm_config_read16 endp

; A word write leaves the neighbouring word (often RW1C status) untouched.
bcm_config_write16 proc address:dword, value:dword
    pushfd
    cli
    push edx
    push ecx
    mov dx, 0cf8h
    in eax, dx
    mov ecx, eax
    mov eax, address
    and al, 0fch
    out dx, eax
    mov edx, address
    and edx, 2
    add dx, 0cfch
    mov eax, value
    out dx, ax
    mov eax, ecx
    mov dx, 0cf8h
    out dx, eax
    pop ecx
    pop edx
    popfd
    ret
bcm_config_write16 endp

bcm_config_write32 proc address:dword, value:dword
    pushfd
    cli
    push edx
    push ecx
    mov dx, 0cf8h
    in eax, dx
    mov ecx, eax
    mov eax, address
    and al, 0fch
    out dx, eax
    mov dx, 0cfch
    mov eax, value
    out dx, eax
    mov eax, ecx
    mov dx, 0cf8h
    out dx, eax
    pop ecx
    pop edx
    popfd
    ret
bcm_config_write32 endp

; Latched PIT channel 0 count; the latch and both reads must not be split.
bcm_pit_count proc
    pushfd
    cli
    xor eax, eax
    out 43h, al                 ; counter 0, latch
    in al, 40h
    mov ah, al
    in al, 40h
    xchg al, ah
    popfd
    ret
bcm_pit_count endp

; int bcm_profile_int(const char *key, int default): [BCM57DBG] in SYSTEM.INI
bcm_profile_int proc key:dword, def:dword
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp
    mov eax, def
    mov esi, offset profile_section
    mov edi, key
    int 20h
    dd 000100abh                ; Get_Profile_Decimal_Int
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    cld
    ret
bcm_profile_int endp

profile_section db "BCM57DBG", 0

; Emulator debug console only; no UART or DOS services are touched.
bcm_debug_char proc character:dword
    push edx
    mov eax, character
    mov dx, 0e9h
    out dx, al
    pop edx
    ret
bcm_debug_char endp

    public BCM57DBG_DDB
    align 4
BCM57DBG_DDB label byte
    dd 0                        ; next DDB
    dw 030ah                    ; Windows 3.1 DDK interface version
    dw 0                        ; no assigned device ID, no public API
    db 0, 1                     ; diagnostic version 0.1
    dw 0
    db "BCM57DBG"
    dd 54004000h                ; WfW MAC_Init_Order, after NDIS 54002000h
    dd offset control_proc
    dd 0, 0, 0, 0, 0, 0, 0    ; V86/PM APIs, reference, service table

    end
