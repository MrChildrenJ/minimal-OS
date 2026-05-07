#include "kernel.h"
#include "common.h"

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef uint32_t size_t;

// compiler sees extern      -> undefined ref
// linker sees undefined ref -> go to linker script / obj file to find symbol -> fill in
// extern ~= trust me, these exist
extern char __bss[], __bss_end[], __stack_top[];
extern char __free_ram[], __free_ram_end[];

// == Disk I/O ==
uint32_t virtio_reg_read32(unsigned offset) {
    // volatile: prevent the compiler from optimizing out the read/write operations
    // in MMIO, memory access may trigger side effects (e.g., sending a command to the device
    return *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset));
}

uint64_t virtio_reg_read64(unsigned offset) {
    return *((volatile uint64_t *) (VIRTIO_BLK_PADDR + offset));
}

void virtio_reg_write32(unsigned offset, uint32_t value) {
    *((volatile uint32_t *) (VIRTIO_BLK_PADDR + offset)) = value;
}

void virtio_reg_fetch_and_or32(unsigned offset, uint32_t value) {
    virtio_reg_write32(offset, virtio_reg_read32(offset) | value);
}

struct virtio_virtq *virtq_init(unsigned index) {
    // Allocate a region for the virtqueue.
    paddr_t virtq_paddr = alloc_pages(align_up(sizeof(struct virtio_virtq), PAGE_SIZE) / PAGE_SIZE);
    struct virtio_virtq *vq = (struct virtio_virtq *) virtq_paddr;
    vq->queue_index = index;
    vq->used_index = (volatile uint16_t *) &vq->used.index;
    // Select the queue: Write the virtqueue index (first queue is 0).
    virtio_reg_write32(VIRTIO_REG_QUEUE_SEL, index);
    // Specify the queue size: Write the # of descriptors we'll use.
    virtio_reg_write32(VIRTIO_REG_QUEUE_NUM, VIRTQ_ENTRY_NUM);
    // Write the physical page frame number (not physical address!) of the queue.
    virtio_reg_write32(VIRTIO_REG_QUEUE_PFN, virtq_paddr / PAGE_SIZE);
    return vq;
}

struct virtio_virtq *blk_request_vq;
struct virtio_blk_req *blk_req;
paddr_t blk_req_paddr;
uint64_t blk_capacity;  

void virtio_blk_init(void) {
    if (virtio_reg_read32(VIRTIO_REG_MAGIC) != 0x74726976)
        PANIC("virtio: invalid magic value");
    if (virtio_reg_read32(VIRTIO_REG_VERSION) != 1)
        PANIC("virtio: invalid version");
    if (virtio_reg_read32(VIRTIO_REG_DEVICE_ID) != VIRTIO_DEVICE_BLK)
        PANIC("virtio: invalid device id");

    // 1. Reset the device.
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, 0);
    // 2. Set the ACKNOWLEDGE status bit: We found the device.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACK);
    // 3. Set the DRIVER status bit: We know how to use the device.
    virtio_reg_fetch_and_or32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER);
    // Set our page size: We use 4KB pages. This defines PFN (page frame number) calculation.
    virtio_reg_write32(VIRTIO_REG_PAGE_SIZE, PAGE_SIZE);
    // Initialize a queue for disk read/write requests.
    blk_request_vq = virtq_init(0);
    // 6. Set the DRIVER_OK status bit: We can now use the device!
    virtio_reg_write32(VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_DRIVER_OK);

    // Get the disk capacity.
    blk_capacity = virtio_reg_read64(VIRTIO_REG_DEVICE_CONFIG + 0) * SECTOR_SIZE;
    printf("virtio-blk: capacity is %d bytes\n", (int)blk_capacity);

    // Allocate a region to store requests to the device.
    blk_req_paddr = alloc_pages(align_up(sizeof(*blk_req), PAGE_SIZE) / PAGE_SIZE);
    blk_req = (struct virtio_blk_req *) blk_req_paddr;
}

// Notifies the device that there is a new request. `desc_index` is the index
// of the head descriptor of the new request.
void virtq_kick(struct virtio_virtq *vq, int desc_index) {
    vq->avail.ring[vq->avail.index % VIRTQ_ENTRY_NUM] = desc_index;
    vq->avail.index++;
    __sync_synchronize();
    virtio_reg_write32(VIRTIO_REG_QUEUE_NOTIFY, vq->queue_index);
    vq->last_used_index++;
}

// Returns whether there are requests being processed by the device.
bool virtq_is_busy(struct virtio_virtq *vq) {
    return vq->last_used_index != *vq->used_index;
}

// Reads/writes from/to virtio-blk device.
void read_write_disk(void *buf, unsigned sector, int is_write) {
    if (sector >= blk_capacity / SECTOR_SIZE) {
        printf("virtio: tried to read/write sector=%d, but capacity is %d\n",
              sector, blk_capacity / SECTOR_SIZE);
        return;
    }

    // Construct the request according to the virtio-blk specification.
    blk_req->sector = sector;
    blk_req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    if (is_write)
        memcpy(blk_req->data, buf, SECTOR_SIZE);

    // Construct the virtqueue descriptors (using 3 descriptors).
    struct virtio_virtq *vq = blk_request_vq;
    vq->descs[0].addr = blk_req_paddr;
    vq->descs[0].len = sizeof(uint32_t) * 2 + sizeof(uint64_t);
    vq->descs[0].flags = VIRTQ_DESC_F_NEXT;
    vq->descs[0].next = 1;

    vq->descs[1].addr = blk_req_paddr + offsetof(struct virtio_blk_req, data);
    vq->descs[1].len = SECTOR_SIZE;
    vq->descs[1].flags = VIRTQ_DESC_F_NEXT | (is_write ? 0 : VIRTQ_DESC_F_WRITE);
    vq->descs[1].next = 2;

    vq->descs[2].addr = blk_req_paddr + offsetof(struct virtio_blk_req, status);
    vq->descs[2].len = sizeof(uint8_t);
    vq->descs[2].flags = VIRTQ_DESC_F_WRITE;

    // Notify the device that there is a new request.
    virtq_kick(vq, 0);

    // Wait until the device finishes processing.
    while (virtq_is_busy(vq))
        ;

    // virtio-blk: If a non-zero value is returned, it's an error.
    if (blk_req->status != 0) {
        printf("virtio: warn: failed to read/write sector=%d status=%d\n",
               sector, blk_req->status);
        return;
    }

    // For read operations, copy the data into the buffer.
    if (!is_write)
        memcpy(buf, blk_req->data, SECTOR_SIZE);
}

// == User Mode ==
extern char _binary_shell_bin_start[], _binary_shell_bin_size[];


// == Page Table ==
/* | vpn1 (10) | vpn0 (10) | offset (12) |
     bits 31-22  bits 21-12  bits 11-0

   In table1: PPN = physical frame number of table2
   In table2: PPN = pfn of leaf
   | PPN (22)   | flags (10) |  flags: UXWRV
     bits 31-10    bits 9-0

   physical_address = (leaf_PTE.PPN << 12) | offset
                    = (leaf_PTE.PPN × PAGE_SIZE) + (vaddr & 0xFFF)
*/
void map_page(uint32_t* table1, uint32_t vaddr, paddr_t paddr, uint32_t flags) {
    if (!is_aligned(vaddr, PAGE_SIZE))  PANIC("unaligned vaddr %x", vaddr);
    if (!is_aligned(paddr, PAGE_SIZE))  PANIC("unaligned paddr %x", paddr);

    uint32_t vpn1 = (vaddr >> 22) & 0x3ff;  // most significant 10 bits
    if ((table1[vpn1] & PAGE_V) == 0) {     // if valid-bit == 0
        uint32_t pt_paddr = alloc_pages(1); // create the 1st level page table if it doesn't exist.
        table1[vpn1] = ((pt_paddr / PAGE_SIZE) << 10) | PAGE_V; // the entry should contain the physical page number, not the physical address itself
        // pt_paddr / PAGE_SIZE = strip off the lower 12 bits of offset
        // res << 10 = right shift to leave 10 bits for flags (high 2 bits are always 0)
    }

    // Set the 2nd level page table entry to map the physical page.
    uint32_t vpn0 = (vaddr >> 12) & 0x3ff;  // middle 10 bits
    uint32_t* table0 = (uint32_t *) ((table1[vpn1] >> 10) * PAGE_SIZE);
    table0[vpn0] = ((paddr / PAGE_SIZE) << 10) | flags | PAGE_V;
}

extern char __kernel_base[];

// == User Mode ==
/* __attribute__((naked)) is necessary here!
 * This function is a one-way trip — once sret executes, we jump to user mode and never return.
 * If the compiler auto-generates a prologue that saves registers to the stack, those stack operations corrupt sp
 * Since we're about to hand control over to the user program, 
 * which will reset sp itself — those operations are pointless and could actually cause problems
 */
__attribute__((naked)) void user_entry(void) {
    __asm__ __volatile__(
        /* When sret executes, the CPU sets the PC to the value in sepc = When sret jumps, it'll land at 0x01000000
         * sepc was originally designed for trap handlers — when a trap occurs, hardware saves the faulting PC into it, and the handler uses sret to return. 
         * Here we're repurposing that mechanism in reverse: instead of returning, we're entering user mode.
         * spec: Supervisor Exception Program Counter
         */
        "csrw sepc, %[sepc]        \n"
        
        /* only bit 5 is 1 -> also set spp (Supervisor Previous Privilege, bit 8) to 0
         * SPP = 0 : switch to U-mode
         * SPP = 1 : switch to S-mode
         */
        "csrw sstatus, %[sstatus]  \n"

        /* Supervisor Return
         * 
         */
        "sret                      \n"
        :
        // Load USER_BASE (0x01000000) into any general-purpose reg ("r"), name it sepc, and reference it in the asm as %[sepc]
        : [sepc] "r" (USER_BASE),
          [sstatus] "r" (SSTATUS_SPIE  | SSTATUS_SUM)   // without SSTATUS_SUM, we will get page fault
    );
}

// == Context Switch ==
__attribute__((naked)) void switch_context(uint32_t* prev_sp, uint32_t* next_sp) {
    __asm__ __volatile__(
        // Save callee-saved registers onto the current process's stack.            sp = 1024
        "addi sp, sp, -13 * 4\n" // Allocate stack space for 13 4-byte registers    sp - 13 * 4 = 972
        "sw ra,  0  * 4(sp)\n"   // Save callee-saved registers only
        "sw s0,  1  * 4(sp)\n"
        "sw s1,  2  * 4(sp)\n"
        "sw s2,  3  * 4(sp)\n"
        "sw s3,  4  * 4(sp)\n"
        "sw s4,  5  * 4(sp)\n"
        "sw s5,  6  * 4(sp)\n"
        "sw s6,  7  * 4(sp)\n"
        "sw s7,  8  * 4(sp)\n"
        "sw s8,  9  * 4(sp)\n"
        "sw s9,  10 * 4(sp)\n"
        "sw s10, 11 * 4(sp)\n"
        "sw s11, 12 * 4(sp)\n"  // 直接壓在 process 自己的 kernel stack 上，用 sp 指向它

        // Switch the stack pointer.
        "sw sp, (a0)\n"         // *prev_sp = sp;                                   *prev_sp = 972                    
        "lw sp, (a1)\n"         // Switch stack pointer (sp) here                   sp = value in "a1" reg
        
        // Restore callee-saved registers from the next process's stack.    
        "lw ra,  0  * 4(sp)\n"  // Restore callee-saved registers only
        "lw s0,  1  * 4(sp)\n"
        "lw s1,  2  * 4(sp)\n"
        "lw s2,  3  * 4(sp)\n"
        "lw s3,  4  * 4(sp)\n"
        "lw s4,  5  * 4(sp)\n"
        "lw s5,  6  * 4(sp)\n"
        "lw s6,  7  * 4(sp)\n"
        "lw s7,  8  * 4(sp)\n"
        "lw s8,  9  * 4(sp)\n"
        "lw s9,  10 * 4(sp)\n"
        "lw s10, 11 * 4(sp)\n"
        "lw s11, 12 * 4(sp)\n"
        "addi sp, sp, 13 * 4\n"  // We've popped 13 4-byte registers from the stack
        "ret\n"
    );
}

struct process procs[PROCS_MAX]; // All process control structures

// Before: take pc as arg
// Now: take the ptr to the exec image (image) and the image size (image_size) as args
struct process* create_process(const void *image, size_t image_size) {
    // Find an unused process control structure.
    struct process* curr = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            curr = &procs[i];
            break;
        }
    }

    if (!curr) {
        PANIC("no free process slots");
    }

    // Stack callee-saved registers. These reg values will be restored in the 1st context switch in switch_context
    // / 需要對這個地址做 *--sp，必須是指標才能 dereference
    uint32_t* sp = (uint32_t*) &curr->stack[sizeof(curr->stack)];   // uint8_t* -> uint32_t*
    for (int i = 11; i >= 0; i--) {
        *--sp = 0;  // set s0 to s11 as zero
    }

    *--sp = (uint32_t) user_entry;

    // Map kernel pages
    uint32_t* page_table = (uint32_t*) alloc_pages(1);

    for (paddr_t paddr = (paddr_t) __kernel_base; paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE) {
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
    }

    // virtio-blk
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

    /* Map user pages:
     *   Copies the execution image page by page for the specified size and maps it to the process' page table.
     *   If we map the execution image "directly" without copying it, processes of the same application would 
     *   end up sharing the same physical pages. It will ruin the memory isolation.
     */
    for (uint32_t off = 0; off < image_size; off += PAGE_SIZE) {
        paddr_t page = alloc_pages(1);

        // Handle the case where the data to be copied is smaller than the page size
        size_t remaining = image_size - off;
        size_t copy_size = PAGE_SIZE <= remaining ? PAGE_SIZE : remaining;

        // Fill and map the page.
        memcpy((void *) page, image + off, copy_size);
        map_page(page_table, USER_BASE + off, page, PAGE_U | PAGE_R | PAGE_W | PAGE_X);
    }

    // Initialize fields.
    curr->pid = i + 1;              // procs: 1 ~ 8
    curr->state = PROC_RUNNABLE;
    curr->sp = (uint32_t) sp;       // store sp into struct, only needs value, doesn't need ptr semantic
    curr->page_table = page_table;
    return curr;
}

struct process* create_kernel_process(void (*pc)(void)) {
    struct process* curr = NULL;
    int i;
    for (i = 0; i < PROCS_MAX; i++) {
        if (procs[i].state == PROC_UNUSED) {
            curr = &procs[i];
            break;
        }
    }
    if (!curr) PANIC("no free process slots");

    uint32_t* sp = (uint32_t*) &curr->stack[sizeof(curr->stack)];
    for (int j = 11; j >= 0; j--)
        *--sp = 0;
    *--sp = (uint32_t) pc;  // ra points directly to kernel function, no user_entry

    uint32_t* page_table = (uint32_t*) alloc_pages(1);
    for (paddr_t paddr = (paddr_t) __kernel_base; paddr < (paddr_t) __free_ram_end; paddr += PAGE_SIZE)
        map_page(page_table, paddr, paddr, PAGE_R | PAGE_W | PAGE_X);
    map_page(page_table, VIRTIO_BLK_PADDR, VIRTIO_BLK_PADDR, PAGE_R | PAGE_W);

    curr->pid = i + 1;
    curr->state = PROC_RUNNABLE;
    curr->sp = (uint32_t) sp;
    curr->page_table = page_table;
    return curr;
}

// == Context Switch Testing ==
void delay(void) {
    for (int i = 0; i < 30000000; i++)
        __asm__ __volatile__("nop"); // do nothing; nop = no operation
}

struct process *proc_a;
struct process *proc_b;

void proc_a_entry(void) {
    printf("starting process A\n");
    while (1) {
        putchar('A');
        yield();    // 
    }
}

void proc_b_entry(void) {
    printf("starting process B\n");
    while (1) {
        putchar('B');
        yield();
    }
}

// == Scheduler ==
struct process* current_proc; // currently running process
struct process* idle_proc;    // idle process

struct {
    uint32_t count;
    uint32_t total_cycles;
} ctx_switch_stat;

void yield(void) {
    // Search for a runnable process
    struct process* next = idle_proc;
    for (int i = 0; i < PROCS_MAX; i++) {
        struct process* proc = &procs[(current_proc->pid + i) % PROCS_MAX];
        if (proc->state == PROC_RUNNABLE && proc->pid > 0) {    // 1st runnable and not idle
            next = proc;
            break;
        }
    }

    // if there's no runnable process, keep running current process
    if (next == current_proc)
        return;

    // store addr of top of kernel stack to sscratch
    __asm__ __volatile__(
        "sfence.vma\n"              // ensure that changes to the page table are properly completed
        "csrw satp, %[satp]\n"
        "sfence.vma\n"              // clear the cache of page table entries (TLB)
        "csrw sscratch, %[sscratch]\n"
        :
        : [satp] "r" (SATP_SV32 | ((uint32_t) next->page_table / PAGE_SIZE)),
          [sscratch] "r" ((uint32_t) &next->stack[sizeof(next->stack)])
    );

    // context switch
    struct process* prev = current_proc;
    current_proc = next;
    uint32_t t0 = READ_CSR(cycle);
    switch_context(&prev->sp, &next->sp);
    // delta = time this process was off-CPU (suspended until someone switched back)
    ctx_switch_stat.total_cycles += READ_CSR(cycle) - t0;
    ctx_switch_stat.count++;
}

// == Context Switch Benchmark ==
// Two workers ping-pong: A(pid=2) yields → finds B(pid=3), B yields → finds A.
// idle stays suspended until both workers exit.
static void print_cpi(uint32_t cycles, uint32_t instret); // forward decl, defined in syscall section

#define BENCH_ROUNDS 1000
static volatile int bench_counter;
static volatile int bench_workers_done;
static uint32_t bench_oneway_avg;
static uint32_t bench_cycles;
static uint32_t bench_instret;

void bench_worker_pair(void) {
    while (bench_counter < BENCH_ROUNDS) {
        bench_counter++;
        yield();
    }
    bench_workers_done++;
    current_proc->state = PROC_EXITED;
    yield();
    PANIC("unreachable");
}

void run_ctx_switch_bench(void) {
    ctx_switch_stat.count = 0;
    ctx_switch_stat.total_cycles = 0;
    bench_counter = 0;
    bench_workers_done = 0;

    create_kernel_process(bench_worker_pair);  // worker A
    create_kernel_process(bench_worker_pair);  // worker B

    uint32_t t0 = READ_CSR(cycle);
    uint32_t i0 = READ_CSR(instret);
    while (bench_workers_done < 2)
        yield();
    uint32_t total_cycles  = READ_CSR(cycle)   - t0;
    uint32_t total_instret = READ_CSR(instret) - i0;

    bench_oneway_avg = total_cycles / BENCH_ROUNDS;
    bench_cycles     = total_cycles;
    bench_instret    = total_instret;

    // BENCH_ROUNDS total one-way switches between the two workers
    printf("[bench] ctx_switch: %u switches, one-way avg=%u cycles, CPI=",
           (uint32_t) BENCH_ROUNDS, bench_oneway_avg);
    print_cpi(bench_cycles, bench_instret);
    printf("\n");
}


// == Memory Allocation
paddr_t alloc_pages(uint32_t n) {                       // allocate n pages; return "physical" addr of the start of new block
    static paddr_t next_paddr = (paddr_t) __free_ram;   // static variable
    paddr_t paddr = next_paddr;
    next_paddr += n * PAGE_SIZE;

    if (next_paddr > (paddr_t) __free_ram_end)
        PANIC("out of memory");

    memset((void *) paddr, 0, n * PAGE_SIZE); // fill with zeroes
    return paddr;
}


// == sbicall & putchar ==
// arg0 - arg5: input args
// fid: func ID; eid: extension ID
struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid) {
    register long a0 __asm__("a0") = arg0;      // Put arg0 into CPU register a0
    register long a1 __asm__("a1") = arg1;
    register long a2 __asm__("a2") = arg2;
    register long a3 __asm__("a3") = arg3;
    register long a4 __asm__("a4") = arg4;
    register long a5 __asm__("a5") = arg5;
    register long a6 __asm__("a6") = fid;
    register long a7 __asm__("a7") = eid;

    // ecall: S-mode (kernel) -> M-mode (OpenSBI)
    // a2-a7 should be reserved by "callee" who can only modify a0 and a1
    __asm__ __volatile__("ecall"                // triggers a trap into firmware, after ecall, results are stored in:
                        : "=r"(a0), "=r"(a1)    // a0 → error, a1 → return value (written by f/w)
                        : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7) // pass args
                        : "memory");            // Tell compiler: memory may change, don’t optimize around this
    return (struct sbiret){.error = a0, .value = a1};
}

void putchar(char ch) {
    sbi_call(ch, 0, 0, 0, 0, 0, 0, 1 /* call Console Putchar (sbi_console_putchar())*/);
}


// == Exception Handler ==
__attribute__((naked))          // again, no prologue/epilogue
__attribute__((aligned(4)))
void kernel_entry(void) {       // addr of this func is always in stvec reg
    __asm__ __volatile__(
        // Retrieve the kernel stack of the running process from sscratch.
        // csrw sscratch, sp          // store curr sp to sscratch, assuming that sp is kernel stack
        "csrrw sp, sscratch, sp\n"  // swap(sp, sscratch)   see line. 134 
                                    // now sp = kernel (not user) stack of curr running process
                                    // sscratch = original value of sp (user stack) at the time of the exception

        "addi sp, sp, -4 * 31\n"    // Allocate 31-words space for trap_frame struct "on kernel stack"
        "sw ra,  4 * 0(sp)\n"       // all general purpose regs except for sp
        "sw gp,  4 * 1(sp)\n"       // ex: save gp to "sp + 4*1"
        "sw tp,  4 * 2(sp)\n"
        "sw t0,  4 * 3(sp)\n"
        "sw t1,  4 * 4(sp)\n"
        "sw t2,  4 * 5(sp)\n"
        "sw t3,  4 * 6(sp)\n"
        "sw t4,  4 * 7(sp)\n"
        "sw t5,  4 * 8(sp)\n"
        "sw t6,  4 * 9(sp)\n"
        "sw a0,  4 * 10(sp)\n"
        "sw a1,  4 * 11(sp)\n"
        "sw a2,  4 * 12(sp)\n"
        "sw a3,  4 * 13(sp)\n"
        "sw a4,  4 * 14(sp)\n"
        "sw a5,  4 * 15(sp)\n"
        "sw a6,  4 * 16(sp)\n"
        "sw a7,  4 * 17(sp)\n"
        "sw s0,  4 * 18(sp)\n"
        "sw s1,  4 * 19(sp)\n"
        "sw s2,  4 * 20(sp)\n"
        "sw s3,  4 * 21(sp)\n"
        "sw s4,  4 * 22(sp)\n"
        "sw s5,  4 * 23(sp)\n"
        "sw s6,  4 * 24(sp)\n"
        "sw s7,  4 * 25(sp)\n"
        "sw s8,  4 * 26(sp)\n"
        "sw s9,  4 * 27(sp)\n"
        "sw s10, 4 * 28(sp)\n"
        "sw s11, 4 * 29(sp)\n"

        // Retrieve and save the sp at the time of exception.
        "csrr a0, sscratch\n"   // read sp of original running proc to a0 for return
        "sw a0, 4 * 30(sp)\n"   // save to "30th slot" of trap_frame

        // Reset the kernel stack, now sp points to bottom of trap_frame (addi sp, sp, -4 * 31)
        "addi a0, sp, 4 * 31\n" // restore sp to original place
        "csrw sscratch, a0\n"   // store a0's value into sscratch(kernel stack top)

        "mv a0, sp\n"           // store sp's value to a0 reg(1st arg)
        "call handle_trap\n"    // call handle_trap with arg a0, which is a ptr to the trap_frame struct

        "lw ra,  4 * 0(sp)\n"   // recover all regs
        "lw gp,  4 * 1(sp)\n"
        "lw tp,  4 * 2(sp)\n"
        "lw t0,  4 * 3(sp)\n"
        "lw t1,  4 * 4(sp)\n"
        "lw t2,  4 * 5(sp)\n"
        "lw t3,  4 * 6(sp)\n"
        "lw t4,  4 * 7(sp)\n"
        "lw t5,  4 * 8(sp)\n"
        "lw t6,  4 * 9(sp)\n"
        "lw a0,  4 * 10(sp)\n"
        "lw a1,  4 * 11(sp)\n"
        "lw a2,  4 * 12(sp)\n"
        "lw a3,  4 * 13(sp)\n"
        "lw a4,  4 * 14(sp)\n"
        "lw a5,  4 * 15(sp)\n"
        "lw a6,  4 * 16(sp)\n"
        "lw a7,  4 * 17(sp)\n"
        "lw s0,  4 * 18(sp)\n"
        "lw s1,  4 * 19(sp)\n"
        "lw s2,  4 * 20(sp)\n"
        "lw s3,  4 * 21(sp)\n"
        "lw s4,  4 * 22(sp)\n"
        "lw s5,  4 * 23(sp)\n"
        "lw s6,  4 * 24(sp)\n"
        "lw s7,  4 * 25(sp)\n"
        "lw s8,  4 * 26(sp)\n"
        "lw s9,  4 * 27(sp)\n"
        "lw s10, 4 * 28(sp)\n"
        "lw s11, 4 * 29(sp)\n"
        "lw sp,  4 * 30(sp)\n"  // recover sp
        "sret\n"                // Supervisor Return. Return from S-mode trap, CPU jumb back to where exception occured
    );
}

// == File System ==
struct file files[FILES_MAX];
uint8_t disk[DISK_MAX_SIZE];

int oct2int(char *oct, int len) {
    int dec = 0;
    for (int i = 0; i < len; i++) {
        if (oct[i] < '0' || oct[i] > '7')
            break;

        dec = dec * 8 + (oct[i] - '0');
    }
    return dec;
}

struct file *fs_lookup(const char *filename) {
    for (int i = 0; i < FILES_MAX; i++) {
        struct file *file = &files[i];
        if (!strcmp(file->name, filename))
            return file;
    }

    return NULL;
}

void fs_init(void) {
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, false);

    unsigned off = 0;
    for (int i = 0; i < FILES_MAX; i++) {
        struct tar_header *header = (struct tar_header *) &disk[off];
        if (header->name[0] == '\0')
            break;

        if (strcmp(header->magic, "ustar") != 0)
            PANIC("invalid tar header: magic=\"%s\"", header->magic);

        int filesz = oct2int(header->size, sizeof(header->size));
        struct file *file = &files[i];
        file->in_use = true;
        strcpy(file->name, header->name);
        memcpy(file->data, header->data, filesz);
        file->size = filesz;
        printf("file: %s, size=%d\n", file->name, file->size);

        off += align_up(sizeof(struct tar_header) + filesz, SECTOR_SIZE);
    }
}

void fs_flush(void) {
    // Copy all file contents into `disk` buffer.
    memset(disk, 0, sizeof(disk));
    unsigned off = 0;
    for (int file_i = 0; file_i < FILES_MAX; file_i++) {
        struct file *file = &files[file_i];
        if (!file->in_use)
            continue;

        struct tar_header *header = (struct tar_header *) &disk[off];
        memset(header, 0, sizeof(*header));
        strcpy(header->name, file->name);
        strcpy(header->mode, "000644");
        strcpy(header->magic, "ustar");
        strcpy(header->version, "00");
        header->type = '0';

        // Turn the file size into an octal string.
        int filesz = file->size;
        for (int i = sizeof(header->size); i > 0; i--) {
            header->size[i - 1] = (filesz % 8) + '0';
            filesz /= 8;
        }

        // Calculate the checksum.
        int checksum = ' ' * sizeof(header->checksum);
        for (unsigned i = 0; i < sizeof(struct tar_header); i++)
            checksum += (unsigned char) disk[off + i];

        for (int i = 5; i >= 0; i--) {
            header->checksum[i] = (checksum % 8) + '0';
            checksum /= 8;
        }

        // Copy file data.
        memcpy(header->data, file->data, file->size);
        off += align_up(sizeof(struct tar_header) + file->size, SECTOR_SIZE);
    }

    // Write `disk` buffer into the virtio-blk.
    for (unsigned sector = 0; sector < sizeof(disk) / SECTOR_SIZE; sector++)
        read_write_disk(&disk[sector * SECTOR_SIZE], sector, true);

    printf("wrote %d bytes to disk\n", sizeof(disk));
}

// == System Call ==
struct syscall_stat {
    uint32_t count;
    uint32_t total_cycles;
    uint32_t total_instret;
};
struct syscall_stat syscall_stats[6]; // index = SYS_* constant (1-5)

static const char *syscall_names[] = {"?", "putchar", "getchar", "exit", "readfile", "writefile"};

static void print_cpi(uint32_t cycles, uint32_t instret) {
    if (instret == 0) { printf("N/A"); return; }
    printf("%u.%u", cycles / instret, (cycles % instret) * 10 / instret);
}

static void print_syscall_stats(void) {
    printf("--- syscall latency ---\n");
    for (int i = 1; i <= 5; i++) {
        if (syscall_stats[i].count == 0)
            continue;
        if (i == SYS_GETCHAR) {
            printf("  getchar:   count=%u (skipped: dominated by keystroke wait)\n",
                   syscall_stats[i].count);
            continue;
        }
        uint32_t avg_c = syscall_stats[i].total_cycles  / syscall_stats[i].count;
        uint32_t avg_i = syscall_stats[i].total_instret / syscall_stats[i].count;
        printf("  %s: count=%u avg_cycles=%u avg_instret=%u CPI=",
               syscall_names[i], syscall_stats[i].count, avg_c, avg_i);
        print_cpi(avg_c, avg_i);
        printf("\n");
    }
}

void handle_syscall(struct trap_frame *f) {
    uint32_t t0 = READ_CSR(cycle);
    uint32_t i0 = READ_CSR(instret);
    int sysno = f->a3;

    switch (sysno) {
        case SYS_PUTCHAR:
            putchar(f->a0);
            break;
        case SYS_GETCHAR:
            // note: total_cycles includes wait time when no char is available
            while (1) {
                long ch = getchar();
                if (ch >= 0) {
                    f->a0 = ch;
                    break;
                }
                yield();
            }
            break;
        case SYS_EXIT:
            printf("=== Performance Summary ===\n");
            print_syscall_stats();
            printf("--- context switch (boot bench, %u switches) ---\n",
                   (uint32_t) BENCH_ROUNDS);
            printf("  one-way avg=%u cycles, CPI=", bench_oneway_avg);
            print_cpi(bench_cycles, bench_instret);
            printf("\n");
            printf("process %d exited\n", current_proc->pid);
            current_proc->state = PROC_EXITED;
            yield();
            PANIC("unreachable");
        case SYS_READFILE:
        case SYS_WRITEFILE: {
            const char *filename = (const char *) f->a0;
            char *buf = (char *) f->a1;
            int len = f->a2;
            struct file *file = fs_lookup(filename);
            if (!file) {
                printf("file not found: %s\n", filename);
                f->a0 = -1;
                break;
            }

            if (len > (int) sizeof(file->data))
                len = file->size;

            if (sysno == SYS_WRITEFILE) {
                memcpy(file->data, buf, len);
                file->size = len;
                fs_flush();
            } else {
                memcpy(buf, file->data, len);
            }

            f->a0 = len;
            break;
        }
        default:
            PANIC("unexpected syscall a3=%x\n", f->a3);
    }

    // SYS_EXIT never reaches here (yield() doesn't return)
    if (sysno >= 1 && sysno <= 5) {
        syscall_stats[sysno].count++;
        syscall_stats[sysno].total_cycles  += READ_CSR(cycle)   - t0;
        syscall_stats[sysno].total_instret += READ_CSR(instret) - i0;
    }
}

long getchar(void) {
    struct sbiret ret = sbi_call(0, 0, 0, 0, 0, 0, 0, 2);
    return ret.error;
}

void handle_trap(struct trap_frame *f) {    // original stack pointer
    uint32_t scause = READ_CSR(scause);
    uint32_t stval = READ_CSR(stval);
    uint32_t user_pc = READ_CSR(sepc);

    if (scause == SCAUSE_ECALL) {
        handle_syscall(f);
        /* user_pc = sepc points to the program counter that caused the exception, which points to the 
           ecall instruction. If we don't change it, the kernel goes back to the same place, and the 
           ecall instruction is executed repeatedly. */
        user_pc += 4;
    } else {
        PANIC("unexpected trap scause=%x, stval=%x, sepc=%x\n", scause, stval, user_pc);
    }

    WRITE_CSR(sepc, user_pc);
}

void kernel_main(void) {
    memset(__bss, 0, (size_t) __bss_end - (size_t) __bss);

    printf("\n\n");

    // Register trap handler
    WRITE_CSR(stvec, (uint32_t) kernel_entry);        

    uint32_t c0 = READ_CSR(cycle);
    uint32_t i0 = READ_CSR(instret);
    virtio_blk_init();
    uint32_t c1 = READ_CSR(cycle);
    uint32_t i1 = READ_CSR(instret);
    printf("[perf] virtio_blk_init: cycles=%d instret=%d\n", c1 - c0, i1 - i0);

    fs_init();

    char buf[SECTOR_SIZE];
    read_write_disk(buf, 0, false /* read from the disk */);
    printf("first sector: %s\n", buf);

    strcpy(buf, "hello from kernel!!!\n");
    read_write_disk(buf, 0, true /* write to the disk */);

    // Created at startup as process; update from taking pc as arg to taking image & image_size as args
    idle_proc = create_process(NULL, 0);
    
    // With process ID 0
    idle_proc->pid = 0;
    
    // Ensures that the execution context of the boot process is saved and restored as that of the idle process
    current_proc = idle_proc;

    run_ctx_switch_bench();

    create_process(_binary_shell_bin_start, (size_t) _binary_shell_bin_size);

    yield();
    PANIC("switched to idle process");
}

__attribute__((section(".text.boot")))  // place this function in .text.boot
__attribute__((naked))          // No compiler-generated code (prologue/epilogue), we control everything
void boot(void) {               // entry point in linker script
    __asm__ __volatile__(       // __volatile__: don’t optimize
        "mv sp, %[stack_top]\n" // set the stack pointer at the end (highest) address of the stack area
        "j kernel_main\n"       // Jump to the kernel main function
        :
        : [stack_top] "r" (__stack_top) // let compiler pass __stack_top into a reg, then pass to inline assembly
                                        // "r" -> put value in a register ([stack_top] is just a name tag)
    );
}