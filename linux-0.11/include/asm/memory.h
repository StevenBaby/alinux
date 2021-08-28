// 注意!!! memcpy(dest,src,n) 假设段寄存器ds=es=通常数据段
// 在内核中使用的所有函数都基于该假设（ds=es=内核空间，fs=局部数据空间，gs=null）
// 具有良好行为的应用程序也是这样（ds=es=用户数据空间）
// 如果任何用户程序随意改动了 es 寄存器而出错，则并不是由于系统程序错误造成的

// 内存块复制：从源地址 src 处开始复制 n 个字节到目的地址 dest 处
// 参数：dest - 复制的目的地址，src - 复制的源地址，n - 复制字节数
// %0 - edi(目的地址dest)，%1 - esi(源地址src)，%2 - ecx(字节数n)
#define memcpy(dest, src, n) (                                          \
	{                                                                   \
		void *_res = dest;                                              \
		__asm__("cld \n rep \n movsb"                                   \
				:                                                       \
				: "D"((long)(_res)), "S"((long)(src)), "c"((long)(n))); \
		_res;                                                           \
	})
