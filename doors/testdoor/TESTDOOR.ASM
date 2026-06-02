; TESTDOOR.ASM - Minimal DOS BBS door for Mutineer DOSBox runner testing
;
; Communicates via COM1 (serial1), which DOSBox routes to the inherited
; socket via: serial1=nullmodem inhsocket:1 telnet:1
;
; Build:
;   nasm -f bin -o TESTDOOR.COM TESTDOOR.ASM
;
; What it does:
;   1. Initializes COM1 at 9600,N,8,1
;   2. Sends a greeting banner to the caller
;   3. Echos characters until Q or Enter is received
;   4. Sends a goodbye message and exits 0

org 0x100               ; .COM files execute at CS:0100

start:
    ; Initialize COM1: 9600 baud, No parity, 8 data bits, 1 stop bit
    ; INT 14h AH=0: init serial
    ; AL = 1110 0011b = 0xE3
    ;   bits 7-5 = 111 = 9600 baud
    ;   bits 4-3 = 00  = no parity
    ;   bit  2   = 0   = 1 stop bit
    ;   bits 1-0 = 11  = 8 data bits
    mov ah, 0x00
    mov al, 0xE3
    xor dx, dx          ; DX=0 = COM1
    int 0x14

    ; Send banner
    mov si, str_banner
    call serial_puts

    ; Echo loop: read chars from COM1, echo back; Q/Enter exits
.echo_loop:
    call serial_getc    ; returns char in AL
    mov bl, al

    ; Echo character back
    call serial_putc

    ; Check for quit conditions
    cmp bl, 0x0D        ; Enter (CR)
    je .done
    cmp bl, 0x0A        ; LF
    je .done
    cmp bl, 'q'
    je .done
    cmp bl, 'Q'
    je .done
    jmp .echo_loop

.done:
    mov si, str_bye
    call serial_puts

    ; DOS exit
    mov ah, 0x4C
    xor al, al
    int 0x21

; ---------------------------------------------------------------------------
; serial_puts: send null-terminated string (SI) to COM1
; ---------------------------------------------------------------------------
serial_puts:
    lodsb
    test al, al
    jz .sp_done
    call serial_putc
    jmp serial_puts
.sp_done:
    ret

; ---------------------------------------------------------------------------
; serial_putc: send byte in AL to COM1 via INT 14h
; ---------------------------------------------------------------------------
serial_putc:
    push ax
    mov ah, 0x01        ; send character
    xor dx, dx          ; COM1
    int 0x14
    pop ax
    ret

; ---------------------------------------------------------------------------
; serial_getc: receive one byte from COM1; returns in AL
; Polls INT 14h AH=3 (get status) until data ready, then reads
; ---------------------------------------------------------------------------
serial_getc:
.poll:
    mov ah, 0x03        ; get status
    xor dx, dx          ; COM1
    int 0x14
    ; AL = line status; bit 0 = data ready
    test al, 0x01
    jz .poll            ; no data yet, keep polling

    mov ah, 0x02        ; receive character
    xor dx, dx
    int 0x14
    test ah, 0x80       ; bit 7 of AH set = timeout/error
    jnz .poll           ; retry on error
    ret

; ---------------------------------------------------------------------------
; Strings
; ---------------------------------------------------------------------------
str_banner:
    db 13, 10
    db "  +-------------------------------------------+", 13, 10
    db "  |    Mutineer BBS -- Test Door  v1.0        |", 13, 10
    db "  |    DOSBox serial pipe is working!         |", 13, 10
    db "  +-------------------------------------------+", 13, 10
    db 13, 10
    db "  Type any characters. Press Q or Enter to exit.", 13, 10
    db 13, 10, 0

str_bye:
    db 13, 10
    db "  Returning to BBS. Goodbye!", 13, 10, 0
