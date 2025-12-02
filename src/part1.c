#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <asm/barrier.h>

#define PROC_NAME "pmu_stats"

/* Event encodings for Cortex-A72 (ARMv8) PMU */
#define EVT_INSTR_RETIRED   0x08
#define EVT_L1D_ACCESS      0x04
#define EVT_L1D_REFILL      0x03
#define EVT_LLC_REFILL      0x17

#define COUNTER_INSTRUCTIONS 0
#define COUNTER_L1_REF       1
#define COUNTER_L1_MISS      2
#define COUNTER_LLC_MISS     3

#define COUNTER_MASK ((1U << COUNTER_INSTRUCTIONS) | \
                      (1U << COUNTER_L1_REF) | \
                      (1U << COUNTER_L1_MISS) | \
                      (1U << COUNTER_LLC_MISS))

#define PMU_ENABLE_BIT    BIT(0)
#define PMU_RESET_EVENTS  BIT(1)
#define PMU_RESET_CYCLES  BIT(2)
#define PMU_CYCLE_COUNTER BIT(31)

struct pmu_counts {
    u64 instructions;
    u64 l1_ref;
    u64 l1_miss;
    u64 llc_miss;
    u64 cycles;
};

static struct proc_dir_entry *pmu_proc;

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

static void pmu_reset_cpu(void *unused)
{
    /* Disable and clear any stale state before programming. */
    write_pmcntenclr_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
    write_pmovsclr_el0(~0U);

    /* Enable PMU + reset both event and cycle counters. */
    write_pmcr_el0(PMU_ENABLE_BIT | PMU_RESET_EVENTS | PMU_RESET_CYCLES);

    pmu_program_counter(COUNTER_INSTRUCTIONS, EVT_INSTR_RETIRED);
    pmu_program_counter(COUNTER_L1_REF, EVT_L1D_ACCESS);
    pmu_program_counter(COUNTER_L1_MISS, EVT_L1D_REFILL);
    pmu_program_counter(COUNTER_LLC_MISS, EVT_LLC_REFILL);

    write_pmcntenset_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
}

static void pmu_disable_cpu(void *unused)
{
    write_pmcntenclr_el0(COUNTER_MASK | PMU_CYCLE_COUNTER);
}

static void pmu_snapshot(struct pmu_counts *snapshot)
{
    preempt_disable();
    snapshot->instructions = read_event_counter(COUNTER_INSTRUCTIONS);
    snapshot->l1_ref = read_event_counter(COUNTER_L1_REF);
    snapshot->l1_miss = read_event_counter(COUNTER_L1_MISS);
    snapshot->llc_miss = read_event_counter(COUNTER_LLC_MISS);
    snapshot->cycles = read_pmccntr_el0();
    preempt_enable();
}

static int pmu_proc_show(struct seq_file *m, void *v)
{
    struct pmu_counts counts;

    pmu_snapshot(&counts);

    seq_printf(m, "instructions: %llu\n", counts.instructions);
    seq_printf(m, "l1_references: %llu\n", counts.l1_ref);
    seq_printf(m, "l1_misses: %llu\n", counts.l1_miss);
    seq_printf(m, "llc_misses: %llu\n", counts.llc_miss);
    seq_printf(m, "cycles: %llu\n", counts.cycles);

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

static int __init pmu_init(void)
{
    pr_info("pmu: programming counters for Raspberry Pi 4\n");

    on_each_cpu(pmu_reset_cpu, NULL, 1);

    pmu_proc = proc_create(PROC_NAME, 0444, NULL, &pmu_proc_fops);
    if (!pmu_proc) {
        on_each_cpu(pmu_disable_cpu, NULL, 1);
        return -ENOMEM;
    }

    return 0;
}

static void __exit pmu_exit(void)
{
    if (pmu_proc)
        proc_remove(pmu_proc);

    on_each_cpu(pmu_disable_cpu, NULL, 1);
    pr_info("pmu: module unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student");
MODULE_DESCRIPTION("ARM PMU monitor for Raspberry Pi 4 (Part 1)");

module_init(pmu_init);
module_exit(pmu_exit);
