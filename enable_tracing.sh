#!/bin/sh
# Allow writing to the kernel trace log.
chmod o+rx /sys/kernel/debug
chmod a+w /sys/kernel/debug/tracing

chmod 0664 /sys/kernel/debug/tracing/trace_clock
chmod 0664 /sys/kernel/debug/tracing/buffer_size_kb
chmod 0664 /sys/kernel/debug/tracing/options/overwrite
chmod 0664 /sys/kernel/debug/tracing/events/sched/sched_switch/enable
chmod 0664 /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable
chmod 0664 /sys/kernel/debug/tracing/events/power/cpu_frequency/enable
chmod 0664 /sys/kernel/debug/tracing/events/power/cpu_idle/enable
chmod 0664 /sys/kernel/debug/tracing/events/cpufreq_interactive/enable
chmod 0664 /sys/kernel/debug/tracing/tracing_on

# Allow only the shell group to read and truncate the kernel trace.
chmod 0660 /sys/kernel/debug/tracing/trace
