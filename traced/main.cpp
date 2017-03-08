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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

// ### remove Qt dep
#include <QDebug>
#include <QCoreApplication>
#include <QByteArray>
#include <QSocketNotifier>

#include "CTraceMessages.h"

const int ShmChunkSize = 1024 * 10;

void controlReader(int cmdfd)
{
    int lcmd;
    char cmd[128];

    if ((lcmd = ::read(cmdfd, cmd, sizeof(cmd)-1)) < 0)
        return;
    if (lcmd > 0 && cmd[lcmd-1] == '\n')
        lcmd--;
    cmd[lcmd] = '\0';

    int shm_fd;
    char *ptr;
    int i;
    const char *name = cmd;

    shm_fd = shm_open(name, O_RDONLY, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        fprintf(stderr, "shm_open %s", name);
        abort();
    }

    ptr = (char*)mmap(0, ShmChunkSize, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap");
        abort();
    }

    ChunkHeader *h = (ChunkHeader*)ptr;
    ptr += sizeof(ChunkHeader);

    if (h->version != TRACED_PROTOCOL_VERSION) {
        fprintf(stderr, "malformed chunk! version %d\n", h->version);
        return;
    }

    // ### don't write the array markers as part of chunk processing
    printf("[\n");
    QByteArray ba = ptr;
    QList<QByteArray> lines = ba.split('\n');

    // ### strip last ,
    for (const QByteArray &line : lines) {
        if (line.isEmpty())
            break;
        switch (line[0]) {
        case 'B': {
            int time = line.split('|').at(1).toInt();
            QByteArray name = line.split('|').at(2);
            printf("{\"pid\":%d,\"tid\":%d,\"ts\":%llu,\"ph\":\"B\",\"cat\":\"\",\"name\":\"%s\"},\n", h->pid, h->tid, time, name.constData());
            break;

        }
        case 'E': {
            int time = line.split('|').at(1).toInt();
            QByteArray name = line.split('|').at(2);
            printf("{\"pid\":%d,\"tid\":%d,\"ts\":%llu,\"ph\":\"E\",\"cat\":\"\",\"name\":\"%s\"},\n", h->pid, h->tid, time, name.constData());
            break;
        }
        default:
            abort();
        }
    }

    printf("]\n");

    if (shm_unlink(name) == -1) {
        fprintf(stderr, "shm_unlink");
        abort();
    }
}

int main(int argc, char **argv) 
{
    QCoreApplication app(argc, argv);
    // Open the remote control interface.
    mknod("/tmp/traced", S_IFIFO | 0666, 0);
    QObject::connect(new QSocketNotifier(open("/tmp/traced", O_RDWR), QSocketNotifier::Read),
            &QSocketNotifier::activated, &controlReader);

    return app.exec();
}
