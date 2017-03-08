/*
 * Copyright (c) 2017 Crimson AS <info@crimson.no>
 * Author: Robin Burchell <robin.burchell@crimson.no>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <sys/socket.h>
#include <sys/time.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "CSystrace.h"
#include "CTraceMessages.h"

// Information about SHM chunks
const int ShmChunkSize = 1024 * 10;
static thread_local int shm_fd = -1;
static thread_local char *shm_ptr = 0;
static thread_local char *current_chunk_name = 0;
static thread_local int remaining_chunk_size;

// FD to communicate with traced
static int traced_fd = 0;

//gettid(); except that mac sucks
#include <unistd.h> // syscall()
#include <sys/syscall.h> // SYS_thread_selfid

static int gettid()
{
    return syscall(SYS_thread_selfid);
}

/*! Update the book keeping for the current position in the chunk.
 */
static void advance_chunk(int len)
{
    shm_ptr += len;
    remaining_chunk_size -= len;
    assert(remaining_chunk_size >= 0);
}

/*!
 * Send the current chunk to traced for processing.
 */
static void submit_chunk()
{
    if (shm_fd == -1)
        return;

    shutdown(shm_fd, SHUT_WR);
    close(shm_fd);
    shm_fd = -1;
    shm_ptr = 0;

    char buf[1024];
    int blen = sprintf(buf, "%s\n", current_chunk_name);
    printf("Sending %s", buf);
    int ret = write(traced_fd, buf, blen);
    if (ret == -1) {
        perror("Can't write to traced!");
    }
    
    free((void*)current_chunk_name);
    current_chunk_name = 0;
}

static int chunk_count = 0;

static void systrace_debug()
{
    static thread_local bool in_debug = false;
    if (in_debug)
        return;

    in_debug = true;
    systrace_record_counter("systrace",  "remainingChunkSize",  remaining_chunk_size);
    systrace_record_counter("systrace", "chunkCount", chunk_count);
    in_debug = false;
}

/*!
 * Make sure we have a valid SHM chunk to write events to, or abort if not.
 */
static void ensure_chunk(int mlen)
{
    if (shm_fd != -1 && remaining_chunk_size >= mlen)
        return;

    if (shm_fd != -1) {
        submit_chunk();
    }

    // ### linux via /dev/shm or memfd_create
    // ### multiple processes!
    asprintf(&current_chunk_name, "tracechunk-%d", chunk_count++);
    shm_unlink(current_chunk_name);
    shm_fd = shm_open(current_chunk_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        perror("Can't open SHM!");
        abort();
    }
    if (ftruncate(shm_fd, ShmChunkSize) == -1) {
        perror("Can't ftruncate SHM!");
        abort();
    }
    shm_ptr = (char*)mmap(0, ShmChunkSize, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("Can't map SHM!");
        abort();
    }
    remaining_chunk_size = ShmChunkSize;

    ChunkHeader *h = (ChunkHeader*)shm_ptr;
    h->version = TRACED_PROTOCOL_VERSION;
    h->pid = getpid();
    h->tid = gettid();
    advance_chunk(sizeof(ChunkHeader));
}

__attribute__((constructor)) void systrace_init()
{
    // ### cleanup hack
    for (int i = 0; i < 9999; ++i) {
        char buf[1024];
        sprintf(buf, "tracechunk-%d", i);
        shm_unlink(buf);
    }

    if (getenv("TRACED") == NULL) {
        traced_fd = open("/tmp/traced", O_WRONLY);
        if (traced_fd == -1) {
            perror("Can't open connection to traced command socket!");
            //abort();
        }
    }
}

__attribute__((destructor)) void systrace_deinit()
{
    if (traced_fd == -1)
        return;
    submit_chunk();
    close(traced_fd);
    traced_fd = -1;
}

int systrace_should_trace(const char *module)
{
    // hack this if you want to temporarily omit some traces.
    return 1;
}

static uint64_t getBareMicroseconds()
{
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}
static uint64_t originalMicroseconds = getBareMicroseconds();
static uint64_t getMicroseconds()
{
    // ### use monotonic time instead
    return getBareMicroseconds() - originalMicroseconds;
}


// ### use "X" events?
void systrace_duration_begin(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk(sizeof(BeginMessage) + 1);
    *shm_ptr = 'B';
    advance_chunk(1);
    BeginMessage *m = (BeginMessage*)shm_ptr;
    m->microseconds = getMicroseconds();
    strncpy(m->tracepoint, tracepoint, MAX_TRACEPOINT_LENGTH);
    advance_chunk(sizeof(BeginMessage));

    systrace_debug();
}

void systrace_duration_end(const char *module, const char *tracepoint)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk(sizeof(EndMessage) + 1);
    *shm_ptr = 'E';
    advance_chunk(1);
    EndMessage *m = (EndMessage*)shm_ptr;
    m->microseconds = getMicroseconds();
    strncpy(m->tracepoint, tracepoint, MAX_TRACEPOINT_LENGTH);
    advance_chunk(sizeof(EndMessage));

    systrace_debug();
}

void systrace_record_counter(const char *module, const char *tracepoint, int value)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk(sizeof(CounterMessage) + 1);
    *shm_ptr = 'C';
    advance_chunk(1);
    CounterMessage *m = (CounterMessage*)shm_ptr;
    m->microseconds = getMicroseconds();
    strncpy(m->tracepoint, tracepoint, MAX_TRACEPOINT_LENGTH);
    m->value = value;
    advance_chunk(sizeof(CounterMessage));

    systrace_debug();
}

void systrace_async_begin(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk(sizeof(AsyncBeginMessage) + 1);
    *shm_ptr = 'b';
    advance_chunk(1);
    AsyncBeginMessage *m = (AsyncBeginMessage*)shm_ptr;
    m->microseconds = getMicroseconds();
    strncpy(m->tracepoint, tracepoint, MAX_TRACEPOINT_LENGTH);
    m->cookie = (intptr_t)cookie;
    advance_chunk(sizeof(AsyncBeginMessage));

    systrace_debug();
}

void systrace_async_end(const char *module, const char *tracepoint, const void *cookie)
{
    if (!systrace_should_trace(module))
        return;

    ensure_chunk(sizeof(AsyncEndMessage) + 1);
    *shm_ptr = 'e';
    advance_chunk(1);
    AsyncEndMessage *m = (AsyncEndMessage*)shm_ptr;
    m->microseconds = getMicroseconds();
    strncpy(m->tracepoint, tracepoint, MAX_TRACEPOINT_LENGTH);
    m->cookie = (intptr_t)cookie;
    advance_chunk(sizeof(AsyncEndMessage));

    systrace_debug();
}


