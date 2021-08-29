// 该文件中含有一些软盘控制器的一些定义，这些信息有多处来源
// 大多数取自 Sanches 和 Canton  编著的 《IBM 微型计算机：程序员手册》一书

#ifndef _FDREG_H
#define _FDREG_H

// 一些软盘类型函数的原型说明
extern int ticks_to_floppy_on(unsigned int nr);
extern void floppy_on(unsigned int nr);
extern void floppy_off(unsigned int nr);
extern void floppy_select(unsigned int nr);
extern void floppy_deselect(unsigned int nr);

// 下面是有关软盘控制器一些端口和符号的定义

// 软盘控制器(FDC)寄存器端口。摘自 S&C 书中约 340 页
#define FD_STATUS 0x3f4 // 主状态寄存器端口
#define FD_DATA 0x3f5   // 数据端口
#define FD_DOR 0x3f2    // 数字输出寄存器（也称为数字控制寄存器）
#define FD_DIR 0x3f7    // 数字输入寄存器
#define FD_DCR 0x3f7    // 数据传输率控制寄存器

// 主状态寄存器各比特位的含义
#define STATUS_BUSYMASK 0x0F // 驱动器忙位（每位对应一个驱动器）
#define STATUS_BUSY 0x10     // 软盘控制器忙
#define STATUS_DMA 0x20      // 0 - 为 DMA 数据传输模式，1 - 为非 DMA 模式
#define STATUS_DIR 0x40      // 传输方向：0 - CPU -> FDC，1 - 相反
#define STATUS_READY 0x80    // 数据寄存器就绪位

// 状态字节0（ST0）各比特位的含义
#define ST0_DS 0x03  // 驱动器选择号（发生中断时驱动器号）
#define ST0_HA 0x04  // 磁头号
#define ST0_NR 0x08  // 磁盘驱动器未准备好
#define ST0_ECE 0x10 // 设备检测出错（零磁道校准出错）
#define ST0_SE 0x20  // 寻道或重新校正操作执行结束

// 中断代码位（中断原因）
// 00 - 命令正常结束；
// 01 - 命令异常结束；
// 10 - 命令无效；
// 11 - FDD 就绪状态改变
#define ST0_INTR 0xC0

// 状态字节1（ST1）各比特位的含义
#define ST1_MAM 0x01 // 未找到地址标志(ID AM)
#define ST1_WP 0x02  // 写保护
#define ST1_ND 0x04  // 未找到指定的扇区
#define ST1_OR 0x10  // 数据传输超时（DMA 控制器故障）
#define ST1_CRC 0x20 // CRC 检验出错
#define ST1_EOC 0x80 // 访问超过一个磁道上的最大扇区号

// 状态字节2（ST2）各比特位的含义
#define ST2_MAM 0x01 // 未找到数据地址标志
#define ST2_BC 0x02  // 磁道坏
#define ST2_SNS 0x04 // 检索（扫描）条件不满足
#define ST2_SEH 0x08 // 检索条件满足
#define ST2_WC 0x10  // 磁道（柱面）号不符
#define ST2_CRC 0x20 // 数据场CRC 校验错
#define ST2_CM 0x40  // 读数据遇到删除标志

// 状态字节3（ST3）各比特位的含义
#define ST3_HA 0x04 // 磁头号
#define ST3_TZ 0x10 // 零磁道信号
#define ST3_WP 0x40 // 写保护

// 软盘命令码
#define FD_RECALIBRATE 0x07 // 重新校正(磁头退到零磁道)
#define FD_SEEK 0x0F        // 磁头寻道
#define FD_READ 0xE6        // 读数据（MT 多磁道操作，MFM 格式，跳过删除数据）
#define FD_WRITE 0xC5       // 写数据（MT，MFM）
#define FD_SENSEI 0x08      // 检测中断状态
#define FD_SPECIFY 0x03     // 设定驱动器参数（步进速率、磁头卸载时间等）

// DMA 命令
#define DMA_READ 0x46  // DMA 读盘，DMA 方式字（送DMA 端口12，11）
#define DMA_WRITE 0x4A // DMA 写盘，DMA 方式字

#endif
