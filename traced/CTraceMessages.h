#ifndef CTRACEMESSAGES_H
#define CTRACEMESSAGES_H

#define TRACED_PROTOCOL_VERSION 256

struct ChunkHeader
{
    int version;
    int pid;
    int tid;
};

#endif // CTRACEMESSAGES_H
