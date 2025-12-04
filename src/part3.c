#include <linux/bitops.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <asm/barrier.h>

#define PROC_NAME_STATS   "pmu_stats"
#define PROC_NAME_CONTROL "pmu_control"

/* Event encodings for Cortex-A72 (ARMv8) PMU */
#define EVT_INSTR_RETIRED   0x08
#define EVT_L1I_REFILL      0x01
#define EVT_L1I_ACCESS      0x14
#define EVT_L1D_REFILL      0x03
#define EVT_L1D_ACCESS      0x04
#define EVT_LLC_REFILL      0x17

#define COUNTER_INSTRUCTIONS 0
#define COUNTER_L1I_REF      1
#define COUNTER_L1I_MISS     2
#define COUNTER_L1D_REF      3
#define COUNTER_L1D_MISS     4
#define COUNTER_LLC_MISS     5

#define COUNTER_MASK (BIT(COUNTER_INSTRUCTIONS) |      \
                      BIT(COUNTER_L1I_REF)   |         \
                      BIT(COUNTER_L1I_MISS)  |         \
                      BIT(COUNTER_L1D_REF)   |         \
                      BIT(COUNTER_L1D_MISS)  |         \
                      BIT(COUNTER_LLC_MISS))

#define PMU_ENABLE_BIT    BIT(0)
#define PMU_RESET_EVENTS  BIT(1)
#define PMU_RESET_CYCLES  BIT(2)
#define PMU_CYCLE_COUNTER BIT(31)

struct pmu_counts {
    u64 instructions;
    u64 l1i_ref;
    u64 l1i_miss;
    u64 l1d_ref;
    u64 l1d_miss;
    u64 llc_miss;
    u64 cycles;
};

static struct proc_dir_entry *pmu_proc_stats;
static struct proc_dir_entry *pmu_proc_ctrl;

/* pause / resume 상태 관리 */
enum pmu_state {
    PMU_STOPPED = 0,
    PMU_RUNNING = 1,
};

static enum pmu_state pmu_state = PMU_STOPPED;
static DEFINE_MUTEX(pmu_ctrl_lock);

/* Raw register helpers */
static inline void write_pmselr_el0(u64 val)
{
    asm volatile("msr pmselr_el0, %0" :: "r"(val));
    isb();
}

static inline void write_pmxevtyper_el0(u64 val)
{
    asm volatile("msr pmxevtyper_el0, %0" :: "r"(val));
    isb();
}

static inline void write_pmxevcntr_el0(u64 val)
{
    asm volatile("msr pmxevcntr_el0, %0" :: "r"(val));
    isb();
}

static inline u64 read_pmxevcntr_el0(void)
{
    u64 val;

    asm volatile("mrs %0, pmxevcntr_el0" : "=r"(val));
    return val;
}

static inline u64 read_pmccntr_el0(void)
{
    u64 val;

    asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
    return val;
}

static inline void write_pmcr_el0(u64 val)
{
    asm volatile("msr pmcr_el0, %0" :: "r"(val));
    isb();
}

static inline void write_pmcntenset_el0(u64 val)
{
    asm volatile("msr pmcntenset_el0, %0" :: "r"(val));
    isb();
}

static inline void write_pmcntenclr_el0(u64 val)
{
    asm volatile("msr pmcntenclr_el0, %0" :: "r"(val));
    isb();
}

static inline void write_pmovsclr_el0(u64 val)
{
    asm volatile("msr pmovsclr_el0, %0" :: "r"(val));
    isb();
}

static inline u64 read_event_counter(u32 counter)
{
    write_pmselr_el0(counter);
    return read_pmxevcntr_el0();
}

static void pmu_program_counter(u32 counter, u32 event)
{
    write_pmselr_el0(counter);
    write_pmxevtyper_el0(event);
    write_pmxevcntr_el0(0);
}

/*
 * per-CPU 초기화 + reset + enable
 * → Part 1에서 쓰던 pmu_reset_cpu 그대로 사용
 */
static void pmu_reset_cpu(void *unused)
{
    /* Disable and clear any stale state before programming. */
    write_pmcntenclr_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
    write_pmovsclr_el0(~0U);

    /* Enable PMU + reset both event and cycle counters. */
    write_pmcr_el0(PMU_ENABLE_BIT | PMU_RESET_EVENTS | PMU_RESET_CYCLES);

    pmu_program_counter(COUNTER_INSTRUCTIONS, EVT_INSTR_RETIRED);
    pmu_program_counter(COUNTER_L1I_REF,      EVT_L1I_ACCESS);
    pmu_program_counter(COUNTER_L1I_MISS,     EVT_L1I_REFILL);
    pmu_program_counter(COUNTER_L1D_REF,      EVT_L1D_ACCESS);
    pmu_program_counter(COUNTER_L1D_MISS,     EVT_L1D_REFILL);
    pmu_program_counter(COUNTER_LLC_MISS,     EVT_LLC_REFILL);

    write_pmcntenset_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
}

static void pmu_disable_cpu(void *unused)
{
    write_pmcntenclr_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
}

/* Part 3용 helper: 전체 CPU start/stop 래퍼 */
static void pmu_start_all_cpus(void)
{
    on_each_cpu(pmu_reset_cpu, NULL, 1);
    pmu_state = PMU_RUNNING;
}

static void pmu_stop_all_cpus(void)
{
    on_each_cpu(pmu_disable_cpu, NULL, 1);
    pmu_state = PMU_STOPPED;
}

static void pmu_read_local(struct pmu_counts *snapshot)
{
    preempt_disable();
    snapshot->instructions = read_event_counter(COUNTER_INSTRUCTIONS);
    snapshot->l1i_ref      = read_event_counter(COUNTER_L1I_REF);
    snapshot->l1i_miss     = read_event_counter(COUNTER_L1I_MISS);
    snapshot->l1d_ref      = read_event_counter(COUNTER_L1D_REF);
    snapshot->l1d_miss     = read_event_counter(COUNTER_L1D_MISS);
    snapshot->llc_miss     = read_event_counter(COUNTER_LLC_MISS);
    snapshot->cycles       = read_pmccntr_el0();
    preempt_enable();
}

static void pmu_collect_cpu(void *info)
{
    struct pmu_counts *per_cpu_counts = info;
    unsigned int cpu = smp_processor_id();

    pmu_read_local(&per_cpu_counts[cpu]);
}

/* ---------- /proc/pmu_stats ---------- */

static int pmu_proc_show(struct seq_file *m, void *v)
{
    struct pmu_counts total = {};
    struct pmu_counts *per_cpu_counts;
    unsigned int cpu;

    per_cpu_counts = kcalloc(nr_cpu_ids, sizeof(*per_cpu_counts), GFP_KERNEL);
    if (!per_cpu_counts)
        return -ENOMEM;

    on_each_cpu(pmu_collect_cpu, per_cpu_counts, 1);

    for_each_online_cpu(cpu) {
        total.instructions += per_cpu_counts[cpu].instructions;
        total.l1i_ref      += per_cpu_counts[cpu].l1i_ref;
        total.l1i_miss     += per_cpu_counts[cpu].l1i_miss;
        total.l1d_ref      += per_cpu_counts[cpu].l1d_ref;
        total.l1d_miss     += per_cpu_counts[cpu].l1d_miss;
        total.llc_miss     += per_cpu_counts[cpu].llc_miss;
        total.cycles       += per_cpu_counts[cpu].cycles;
    }

    kfree(per_cpu_counts);

    seq_printf(m, "instructions: %llu\n", total.instructions);
    seq_printf(m, "l1i_references: %llu\n", total.l1i_ref);
    seq_printf(m, "l1i_misses: %llu\n", total.l1i_miss);
    seq_printf(m, "l1d_references: %llu\n", total.l1d_ref);
    seq_printf(m, "l1d_misses: %llu\n", total.l1d_miss);
    seq_printf(m, "llc_misses: %llu\n", total.llc_miss);
    seq_printf(m, "cycles: %llu\n", total.cycles);
    seq_printf(m, "state: %s\n",
               (pmu_state == PMU_RUNNING) ? "running" : "stopped");

    return 0;
}

static int pmu_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, pmu_proc_show, NULL);
}

static const struct proc_ops pmu_proc_fops = {
    .proc_open    = pmu_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ---------- /proc/pmu_control (Part 3) ---------- */

static ssize_t pmu_ctrl_write(struct file *file,
                              const char __user *buf,
                              size_t len, loff_t *ppos)
{
    char kbuf[16];

    if (len >= sizeof(kbuf))
        len = sizeof(kbuf) - 1;

    if (copy_from_user(kbuf, buf, len))
        return -EFAULT;

    kbuf[len] = '\0';

    mutex_lock(&pmu_ctrl_lock);

    if (!strncmp(kbuf, "1", 1) || !strncmp(kbuf, "start", 5) ||
        !strncmp(kbuf, "reset", 5)) {
        pr_info("pmu: start/reset counters\n");
        pmu_start_all_cpus();
    } else if (!strncmp(kbuf, "0", 1) || !strncmp(kbuf, "stop", 4) ||
               !strncmp(kbuf, "pause", 5)) {
        pr_info("pmu: stop counters\n");
        pmu_stop_all_cpus();
    } else {
        pr_warn("pmu: unknown control command: %s\n", kbuf);
        mutex_unlock(&pmu_ctrl_lock);
        return -EINVAL;
    }

    mutex_unlock(&pmu_ctrl_lock);
    return len;
}

static const struct proc_ops pmu_ctrl_fops = {
    .proc_write = pmu_ctrl_write,
};

/* ---------- module init / exit ---------- */

static int __init pmu_init(void)
{
    pr_info("pmu: programming counters for Raspberry Pi 4\n");

    /* 모듈 로드 시 한 번 reset + 시작 (Part 1과 동일한 초기 동작) */
    pmu_start_all_cpus();

    pmu_proc_stats = proc_create(PROC_NAME_STATS, 0444, NULL, &pmu_proc_fops);
    if (!pmu_proc_stats) {
        pmu_stop_all_cpus();
        return -ENOMEM;
    }

    pmu_proc_ctrl = proc_create(PROC_NAME_CONTROL, 0666, NULL, &pmu_ctrl_fops);
    if (!pmu_proc_ctrl) {
        proc_remove(pmu_proc_stats);
        pmu_stop_all_cpus();
        return -ENOMEM;
    }

    return 0;
}

static void __exit pmu_exit(void)
{
    if (pmu_proc_ctrl)
        proc_remove(pmu_proc_ctrl);
    if (pmu_proc_stats)
        proc_remove(pmu_proc_stats);

    pmu_stop_all_cpus();
    pr_info("pmu: module unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("ARM PMU monitor for Raspberry Pi 4 (Part 1+3: pause/resume)");

module_init(pmu_init);
module_exit(pmu_exit);
