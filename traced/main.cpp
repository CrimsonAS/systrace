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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

// ### remove Qt dep
#include <QDebug>
#include <QCoreApplication>
#include <QByteArray>
#include <QSocketNotifier>
#include <QObject>

#include <unordered_map>

#include "CTraceMessages.h"

const int ShmChunkSize = 1024 * 10;

class TraceClient : public QObject
{
    Q_OBJECT
public:
    int fd;
    QByteArray buf;

public slots:
    void readControlSocket();
private:
    bool processChunk(const char *name);
    const char *getString(uint64_t id);

    std::unordered_map<uint64_t, std::string> registeredStrings;
};

const char *TraceClient::getString(uint64_t id)
{
    auto it = registeredStrings.find(id);
    if (it == registeredStrings.end()) {
        return 0;
    }

    return it->second.c_str();
}

bool TraceClient::processChunk(const char *name)
{
    int shm_fd;
    char *ptr;

    shm_fd = shm_open(name, O_RDONLY, S_IRUSR | S_IWUSR);
    if (shm_fd == -1) {
        fprintf(stderr, "shm_open %s: %s\n", name, strerror(errno));
        return false;
    }

    ptr = (char*)mmap(0, ShmChunkSize, PROT_READ, MAP_SHARED, shm_fd, 0);
    if (ptr == MAP_FAILED) {
        fprintf(stderr, "mmap %s\n", strerror(errno));
        abort();
    }

    ChunkHeader *h = (ChunkHeader*)ptr;
    ptr += sizeof(ChunkHeader);

    if (h->version != TRACED_PROTOCOL_VERSION) {
        fprintf(stderr, "malformed chunk! version %d\n", h->version);
        return true;
    }

    while (1) {
        char c = *ptr++;
        switch (c) {
        case 'r': {
            // String registration
            RegisterStringMessage *m = (RegisterStringMessage*)ptr;
            std::string s(&m->stringData, m->length);
            registeredStrings[m->id] = s;
            ptr += sizeof(RegisterStringMessage) + m->length;
            break;
        }
        case 'B': {
            BeginMessage *m = (BeginMessage*)ptr;
            fprintf(stdout, "{\"pid\":%d,\"tid\":%d,\"ts\":%llu,\"ph\":\"B\",\"cat\":\"\",\"name\":\"%s\"},\n", h->pid, h->tid, m->microseconds, getString(m->tracepointId));
            ptr += sizeof(BeginMessage);
            break;
        }
        case 'E': {
            EndMessage *m = (EndMessage*)ptr;
            fprintf(stdout, "{\"pid\":%d,\"tid\":%d,\"ts\":%llu,\"ph\":\"E\",\"cat\":\"\",\"name\":\"%s\"},\n", h->pid, h->tid, m->microseconds, getString(m->tracepointId));
            ptr += sizeof(EndMessage);
            break;
        }
        case 'C': {
            CounterMessage *m = (CounterMessage*)ptr;
            if (m->id == -1)
                fprintf(stdout, "{\"pid\":%d,\"ts\":%llu,\"ph\":\"C\",\"cat\":\"\",\"name\":\"%s\",\"args\":{\"%s\":%d}},\n", h->pid, m->microseconds, getString(m->tracepointId), getString(m->tracepointId), m->value);
            else
                fprintf(stdout, "{\"pid\":%d,\"ts\":%llu,\"ph\":\"C\",\"cat\":\"\",\"name\":\"%s\",\"id\":%d,\"args\":{\"%s\":%d}},\n", h->pid, m->microseconds, getString(m->tracepointId), m->id, getString(m->tracepointId), m->value);
            ptr += sizeof(CounterMessage);
            break;
        }
        case 'b': {
            AsyncBeginMessage *m = (AsyncBeginMessage*)ptr;
            fprintf(stdout, "{\"pid\":%d,\"ts\":%llu,\"ph\":\"b\",\"cat\":\"\",\"name\":\"%s\",\"id\":\"%p\",\"args\":{}},\n", h->pid, m->microseconds, getString(m->tracepointId), (void*)m->cookie);
            ptr += sizeof(AsyncBeginMessage);
            break;
        }
        case 'e': {
            AsyncEndMessage *m = (AsyncEndMessage*)ptr;
            fprintf(stdout, "{\"pid\":%d,\"ts\":%llu,\"ph\":\"e\",\"cat\":\"\",\"name\":\"%s\",\"id\":\"%p\",\"args\":{}},\n", h->pid, m->microseconds, getString(m->tracepointId), (void*)m->cookie);
            ptr += sizeof(AsyncEndMessage);
            break;
        }
        case '\0':
            goto out;
            break;
        default:
            fprintf(stderr, "Unknown token %c\n", c);
            abort();
        }
    }

    fflush(stdout);

out:

    if (shm_unlink(name) == -1) {
        fprintf(stderr, "shm_unlink: %s\n", strerror(errno));
        abort();
    }

    return true;
}

void TraceClient::readControlSocket()
{
    int lcmd;
    char cmd[1024];

    if ((lcmd = ::read(this->fd, cmd, sizeof(cmd)-1)) <= 0) {
        close(this->fd);
        this->deleteLater();
        return;
    }

    buf += QByteArray(cmd, lcmd);
    qDebug() << buf;
    QList<QByteArray> bufs = buf.split('\n');
    for (const QByteArray &abuf : bufs) {
        if (abuf.isEmpty()) {
            buf = "";
            break;
        }
        fprintf(stderr, "Trying chunk %s\n", abuf.constData());
        if (!processChunk(abuf.constData())) {
            // segment got eaten out from under us perhaps
            continue;
        }
        fprintf(stderr, "Done chunk %s\n", abuf.constData());
    }

    printf("\n\n\n\n");
}

int main(int argc, char **argv) 
{
    QCoreApplication app(argc, argv);

    struct sockaddr_un local;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s == -1) {
        perror("Can't create socket");
        abort();
    }

    int optval = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    local.sun_family = AF_UNIX;
    strcpy(local.sun_path, "/tmp/traced");
    unlink(local.sun_path);
    int len = strlen(local.sun_path) + sizeof(local.sun_family) + 1;
    if (bind(s, (struct sockaddr *)&local, len) == -1) {
        perror("Can't bind()");
        abort();
    }

    if (listen(s, 5) == -1) {
        perror("Can't listen");
        abort();
    }

    QObject::connect(new QSocketNotifier(s, QSocketNotifier::Read),
        &QSocketNotifier::activated, [s]() {
        struct sockaddr_un remote;
        int len = sizeof(struct sockaddr_un);
        int client = accept(s, (struct sockaddr*)&remote, (socklen_t *)&len);
        TraceClient *tc = new TraceClient; // ### cleanup
        tc->fd = client;
        QSocketNotifier *csn = new QSocketNotifier(client,  QSocketNotifier::Read);
        csn->setParent(tc);
        QObject::connect(csn, 
            &QSocketNotifier::activated, tc, &TraceClient::readControlSocket);
    });

    printf("[\n");

    return app.exec();
    printf("]\n");
}

#include "main.moc"
