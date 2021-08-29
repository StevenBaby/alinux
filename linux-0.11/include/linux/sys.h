// sys.h 头文件列出了内核中所有系统调用函数的原型，以及系统调用函数指针表

extern int sys_setup();     // 系统启动初始化设置函数
extern int sys_exit();      // 程序退出
extern int sys_fork();      // 创建进程
extern int sys_read();      // 读文件
extern int sys_write();     // 写文件
extern int sys_open();      // 打开文件
extern int sys_close();     // 关闭文件
extern int sys_waitpid();   // 等待进程终止
extern int sys_creat();     // 创建文件
extern int sys_link();      // 创建一个文件的硬连接
extern int sys_unlink();    // 删除一个文件名(或删除文件)
extern int sys_execve();    // 执行程序
extern int sys_chdir();     // 更改当前目录
extern int sys_time();      // 取当前时间
extern int sys_mknod();     // 建立块/字符特殊文件
extern int sys_chmod();     // 修改文件属性
extern int sys_chown();     // 修改文件宿主和所属组
extern int sys_break();     //
extern int sys_stat();      // 使用路径名取文件的状态信息
extern int sys_lseek();     // 重新定位读/写文件偏移
extern int sys_getpid();    // 取进程 id
extern int sys_mount();     // 安装文件系统
extern int sys_umount();    // 卸载文件系统
extern int sys_setuid();    // 设置进程用户 id
extern int sys_getuid();    // 取进程用户 id
extern int sys_stime();     // 设置系统时间日期
extern int sys_ptrace();    // 程序调试
extern int sys_alarm();     // 设置报警
extern int sys_fstat();     // 使用文件句柄取文件的状态信息
extern int sys_pause();     // 暂停进程运行
extern int sys_utime();     // 改变文件的访问和修改时间
extern int sys_stty();      // 修改终端行设置
extern int sys_gtty();      // 取终端行设置信息
extern int sys_access();    // 检查用户对一个文件的访问权限
extern int sys_nice();      // 设置进程执行优先权
extern int sys_ftime();     // 取日期和时间
extern int sys_sync();      // 同步高速缓冲与设备中数据
extern int sys_kill();      // 终止一个进程
extern int sys_rename();    // 更改文件名
extern int sys_mkdir();     // 创建目录
extern int sys_rmdir();     // 删除目录
extern int sys_dup();       // 复制文件句柄
extern int sys_pipe();      // 创建管道
extern int sys_times();     // 取运行时间
extern int sys_prof();      // 程序执行时间区域
extern int sys_brk();       // 修改数据段长度
extern int sys_setgid();    // 设置进程组 id
extern int sys_getgid();    // 取进程组 id
extern int sys_signal();    // 信号处理
extern int sys_geteuid();   // 取进程有效用户 id
extern int sys_getegid();   // 取进程有效组 id
extern int sys_acct();      // 进程记帐
extern int sys_phys();      //
extern int sys_lock();      //
extern int sys_ioctl();     // 设备控制
extern int sys_fcntl();     // 文件句柄操作
extern int sys_mpx();       //
extern int sys_setpgid();   // 设置进程组id
extern int sys_ulimit();    //
extern int sys_uname();     // 显示系统信息
extern int sys_umask();     // 取默认文件创建属性码
extern int sys_chroot();    // 改变根系统
extern int sys_ustat();     // 取文件系统信息
extern int sys_dup2();      // 复制文件句柄
extern int sys_getppid();   // 取父进程 id
extern int sys_getpgrp();   // 取进程组 id，等于 getpgid(0)
extern int sys_setsid();    // 在新会话中运行程序
extern int sys_sigaction(); // 改变信号处理过程
extern int sys_sgetmask();  // 取信号屏蔽码
extern int sys_ssetmask();  // 设置信号屏蔽码
extern int sys_setreuid();  // 设置真实与/或有效用户 id
extern int sys_setregid();  // 设置真实与/或有效组 id

fn_ptr sys_call_table[] = {
    sys_setup,
    sys_exit,
    sys_fork,
    sys_read,
    sys_write,
    sys_open,
    sys_close,
    sys_waitpid,
    sys_creat,
    sys_link,
    sys_unlink,
    sys_execve,
    sys_chdir,
    sys_time,
    sys_mknod,
    sys_chmod,
    sys_chown,
    sys_break,
    sys_stat,
    sys_lseek,
    sys_getpid,
    sys_mount,
    sys_umount,
    sys_setuid,
    sys_getuid,
    sys_stime,
    sys_ptrace,
    sys_alarm,
    sys_fstat,
    sys_pause,
    sys_utime,
    sys_stty,
    sys_gtty,
    sys_access,
    sys_nice,
    sys_ftime,
    sys_sync,
    sys_kill,
    sys_rename,
    sys_mkdir,
    sys_rmdir,
    sys_dup,
    sys_pipe,
    sys_times,
    sys_prof,
    sys_brk,
    sys_setgid,
    sys_getgid,
    sys_signal,
    sys_geteuid,
    sys_getegid,
    sys_acct,
    sys_phys,
    sys_lock,
    sys_ioctl,
    sys_fcntl,
    sys_mpx,
    sys_setpgid,
    sys_ulimit,
    sys_uname,
    sys_umask,
    sys_chroot,
    sys_ustat,
    sys_dup2,
    sys_getppid,
    sys_getpgrp,
    sys_setsid,
    sys_sigaction,
    sys_sgetmask,
    sys_ssetmask,
    sys_setreuid,
    sys_setregid,
};
