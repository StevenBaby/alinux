# alinux
Linux 源码分析

[本项目地址](https://github.com/StevenBaby/alinux)

## 配置开发环境

对于 bochs 的配置不在赘述，可以查阅 **x86 汇编语言** 中相关的内容。

### 克隆代码

执行命令

    git clone https://github.com/StevenBaby/alinux.git

然后可以在 devel 目录执行

    make bochs

或者

    make qemu

来执行模拟程序。

---

具体实现的细节，待续...

## 参考资料

- <https://mirrors.edge.kernel.org/pub/linux/kernel/Historic/old-versions/linux-0.11.tar.gz>

- <http://oldlinux.org/Linux.old/kernel/0.1x/linux-0.11-060618-gcc4.tar.gz>

- bootimage  
    <http://oldlinux.org/Linux.old/bochs/>
- <https://github.com/yuan-xy/Linux-0.11>

- 赵炯 - 《Linux内核完全注释》  
    <https://github.com/wuzhouhui/misc/blob/master/linux-0.11%E5%86%85%E6%A0%B8%E5%AE%8C%E5%85%A8%E6%B3%A8%E9%87%8Av3.pdf>

- How to mount a QEMU virtual disk image  
    <https://www.cloudsavvyit.com/7517/how-to-mount-a-qemu-virtual-disk-image/>

- Make partition using sfdisk  
    <https://www.computerhope.com/unix/sfdisk.htm>

- Install  
    <https://www.hpcf.upr.edu/~humberto/documents/install-log.html>
