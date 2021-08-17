org 0x7c00

xchg bx, bx

mov ah, 3; 获取光标位置
mov bh, 0; 第 0 页
int 0x10

xchg bx, bx
; es: bp 表示字符串的位置
mov ax, 0
mov es, ax
mov bp, message

mov ah, 0x13; 表示打印字符串
mov al, 1; 表示移动光标
mov cx, message.end - message
mov bh, 0
mov bl, 7
int 0x10

xchg bx, bx

read_mbr:
    ; 读取主引导扇区

    ; 缓冲区位 es:bx = 0x10000
    mov ax, 0x1000
    mov es, ax
    mov bx, 0

    mov ah, 2; 表示读磁盘
    mov al, 1; 读取一个扇区
    mov ch, 0; 磁道号 0 
    mov cl, 1; 第一个扇区，注意扇区号从 1 开始
    mov dh, 0; 第 0 个磁头（盘面）
    mov dl, 0; 驱动器号 0 
    int 0x13
    jnc .success
.error:
    ; 读取失败
    jmp .error
.success:
    ; 此时读取成功
    xchg bx, bx

    mov word es:[508], 0x1122
    mov bx, 0

    mov ah, 3; 表示写磁盘
    mov al, 1; 写一个扇区
    mov ch, 0; 磁道号 0 
    mov cl, 1; 第一个扇区，注意扇区号从 1 开始
    mov dh, 0; 第 0 个磁头（盘面）
    mov dl, 0; 驱动器号 0 
    int 0x13

xchg bx, bx;

halt:
    jmp halt

message:
    db "hello world", 10, 13, 0
    .end

times 510 - ($ - $$) db 0
dw 0xaa55
