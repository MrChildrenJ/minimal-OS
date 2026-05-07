#pragma once
#include "common.h"

#define PROCS_MAX 8       // Maximum number of processes
#define PROC_UNUSED   0   // Unused process control structure
#define PROC_RUNNABLE 1   // Runnable process

// == Page Table ==
#define SATP_SV32 (1u << 31)    // single bit in the satp reg, which indicates "enable paging in Sv32 mode"
#define PAGE_V    (1 << 0)      // "Valid" bit (entry is enabled)
#define PAGE_R    (1 << 1)      // Readable
#define PAGE_W    (1 << 2)      // Writable
#define PAGE_X    (1 << 3)      // Executable
#define PAGE_U    (1 << 4)      // User (accessible in user mode)

// == File System ==
#define SSTATUS_SUM  (1 << 18)

// == Disk I/O ==
#define SECTOR_SIZE       512
#define VIRTQ_ENTRY_NUM   16
#define VIRTIO_DEVICE_BLK 2
#define VIRTIO_BLK_PADDR  0x10001000
#define VIRTIO_REG_MAGIC         0x00
#define VIRTIO_REG_VERSION       0x04
#define VIRTIO_REG_DEVICE_ID     0x08
#define VIRTIO_REG_PAGE_SIZE     0x28
#define VIRTIO_REG_QUEUE_SEL     0x30
#define VIRTIO_REG_QUEUE_NUM_MAX 0x34
#define VIRTIO_REG_QUEUE_NUM     0x38
#define VIRTIO_REG_QUEUE_PFN     0x40
#define VIRTIO_REG_QUEUE_READY   0x44
#define VIRTIO_REG_QUEUE_NOTIFY  0x50
#define VIRTIO_REG_DEVICE_STATUS 0x70
#define VIRTIO_REG_DEVICE_CONFIG 0x100
#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTQ_DESC_F_NEXT          1
#define VIRTQ_DESC_F_WRITE         2
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1
#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

// Virtqueue Descriptor Table entry.
struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

// Virtqueue Available Ring.
struct virtq_avail {
    uint16_t flags;
    uint16_t index;
    uint16_t ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue Used Ring entry.
struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

// Virtqueue Used Ring.
struct virtq_used {
    uint16_t flags;
    uint16_t index;
    struct virtq_used_elem ring[VIRTQ_ENTRY_NUM];
} __attribute__((packed));

// Virtqueue.
struct virtio_virtq {
    struct virtq_desc descs[VIRTQ_ENTRY_NUM];
    struct virtq_avail avail;
    struct virtq_used used __attribute__((aligned(PAGE_SIZE)));
    int queue_index;
    volatile uint16_t *used_index;
    uint16_t last_used_index;
} __attribute__((packed));

// Virtio-blk request.
struct virtio_blk_req {
    // 1st descriptor: read-only from device
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;

    // 2nd descriptor: writable by the device if it's a read operation (VIRTQ_DESC_F_WRITE)
    uint8_t data[512];

    // 3rd descriptor: writable by the device (VIRTQ_DESC_F_WRITE)
    uint8_t status;
} __attribute__((packed));  // without padding

// == File System ==
#define FILES_MAX      2
#define DISK_MAX_SIZE  align_up(sizeof(struct file) * FILES_MAX, SECTOR_SIZE)

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char padding[12];
    char data[];      // Array pointing to the data area following the header
                      // (flexible array member)
} __attribute__((packed));

struct file {
    bool in_use;      // Indicates if this file entry is in use
    char name[100];   // File name
    char data[1024];  // File content
    size_t size;      // File size
};

// == User Mode ==
// The base virtual address of an application image. This needs to match the starting address defined in user.ld
#define USER_BASE 0x1000000
// SPIE: Supervisor Previous Interrupt Enable
#define SSTATUS_SPIE (1 << 5)

// == System Call ==
#define SCAUSE_ECALL 8
#define PROC_EXITED   2

// == Process ==
struct process {
    int pid;                // process ID
    int state;              // process state: PROC_UNUSED or PROC_RUNNABLE
    vaddr_t sp;             // Stack pointer
    uint32_t* page_table;   // ptr to the first-level page table.
    uint8_t stack[8192];    // Kernel stack, contains saved CPU regs, return addr (where it was called from), and local variables
};

// sbi: interface between kernel and firmware
struct sbiret {
    long error;
    long value;
};

/*
/ Prints where the panic occurred, then enters an infinite loop to halt processing
/ Why macro i/o func? __FILE__ and __LINE__ must expand at the call site (where it's called)
/ Macro expands at the call site during "preprocessing"
/ if decl func, FILE & LINE always points to panic()'s definition
/ while (1) {} makes the compiler aware that execution never continues past PANIC(), enabling better dead code analysis
/ ##__VA_ARGS__: GCC/Clang extension, defining macros that "accept a variable number of arguments"
/                ## removes the preceding , when the variable arguments are empty
                 Ex: PANIC("oops!") can also successfully compile
*/
#define PANIC(fmt, ...)                                                        \
    do {                                                                       \
        printf("PANIC: %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__);  \
        while (1) {}                                                           \
    } while (0)

// == Exception Handler ==
struct trap_frame {
    uint32_t ra;
    uint32_t gp;
    uint32_t tp;
    uint32_t t0;
    uint32_t t1;
    uint32_t t2;
    uint32_t t3;
    uint32_t t4;
    uint32_t t5;
    uint32_t t6;
    uint32_t a0;
    uint32_t a1;
    uint32_t a2;
    uint32_t a3;
    uint32_t a4;
    uint32_t a5;
    uint32_t a6;
    uint32_t a7;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t sp;
} __attribute__((packed));

#define READ_CSR(reg)                                                          \
    ({                                                                         \
        unsigned long __tmp;                                                   \
        __asm__ __volatile__("csrr %0, " #reg : "=r"(__tmp));                  \
        __tmp;                                                                 \
    })

#define WRITE_CSR(reg, value)                                                  \
    do {                                                                       \
        uint32_t __tmp = (value);                                              \
        __asm__ __volatile__("csrw " #reg ", %0" ::"r"(__tmp));                \
    } while (0)

struct sbiret sbi_call(long arg0, long arg1, long arg2, long arg3, long arg4, long arg5, long fid, long eid);
void putchar(char ch);
long getchar(void);

void yield(void);
paddr_t alloc_pages(uint32_t n);

void map_page(uint32_t *table1, uint32_t vaddr, paddr_t paddr, uint32_t flags);