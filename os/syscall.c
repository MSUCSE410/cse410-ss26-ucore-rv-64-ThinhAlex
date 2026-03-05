#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 va, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	// YOUR CODE
	TimeVal kernel_time;

	uint64 current_cycles = get_cycle();
    kernel_time.sec = current_cycles / CPU_FREQ;
    kernel_time.usec = (current_cycles % CPU_FREQ) * 1000000 / CPU_FREQ;

	struct proc * current_proc = curr_proc();
	if (copyout(current_proc->pagetable, va, (char *)&kernel_time, sizeof(TimeVal)) != 0) {
        return -1; 
    }

	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(uint64 va) {
	struct proc * current_proc = curr_proc();

	TaskInfo kernel_info;
	kernel_info.status = Running;

	uint64 end_time = get_cycle()*1000 / CPU_FREQ;
	kernel_info.time = end_time - current_proc->task_info.time;

	for (int i = 0; i < MAX_SYSCALL_NUM; i++){
		kernel_info.syscall_times[i] = current_proc->task_info.syscall_times[i];
	}

	if (copyout(current_proc->pagetable, va, (char *)&kernel_info, sizeof(TaskInfo)) != 0) {
        return -1;
    }

	return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd) 
{
    if (len == 0) {
		return 0;
	}

    if ((port & ~0x7) != 0 || (port & 0x7) == 0) {
		return -1; 
	} 

	if (!PGALIGNED((uint64) start)) {
		return -1;
	}
    
    int perm = PTE_U | PTE_V;
    if (port & 1) perm |= PTE_R;
    if (port & 2) perm |= PTE_W;
    if (port & 4) perm |= PTE_X;

	struct proc *p = curr_proc();    
    uint64 a = PGROUNDDOWN(start);
    uint64 end = PGROUNDUP(start + len);

    for (uint64 va = a; va < end; va += PGSIZE) {
        if (walkaddr(p->pagetable, va) != 0) {
            return -1; 
        }
    }

    for (uint64 va = a; va < end; va += PGSIZE) {
        void *mem = kalloc();
        if (mem == 0) {
			return -1; 
		}
        
        memset(mem, 0, PGSIZE);

        if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0) {
            kfree(mem);
            return -1;
        }
    }

    return 0;
}

uint64 sys_munmap(uint64 start, uint64 len) 
{
    if (len == 0) {
		return 0;
	}

	if (!PGALIGNED((uint64) start)) {
		return -1;
	}

    struct proc *p = curr_proc();
    uint64 a = PGROUNDDOWN(start);
    uint64 end = PGROUNDUP(start + len);

    for (uint64 va = a; va < end; va += PGSIZE) {
        if (walkaddr(p->pagetable, va) == 0) {
            return -1; 
        }
    }

    uint64 num_pages = (end - a) / PGSIZE;
    uvmunmap(p->pagetable, a, num_pages, 1);

    return 0;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	struct proc * current_proc = curr_proc();
	current_proc->task_info.syscall_times[id] += 1;

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info(args[0]);
		break;

	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;

	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
