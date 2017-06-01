# Introduction

Android has an 'atrace' tool used to collect and return systrace output (by
manipulating debugfs & reading the ftrace buffer). This is a hacked up copy of
it in order to run it on a Linux host.

Start out by running enable_tracing.sh on the target (to turn tracing on & set
permissions). Then build atrace, remembering to link against -lz to get
compression code. Run it (or use some magical wrapper like systrace to run it),
and enjoy. Hopefully.
