#include "CSystrace.h"
#include <unistd.h>

int main(int, char **) 
{
    systrace_init();
    TRACE_EVENT0("app", "main");
    {

        TRACE_COUNTER1("app", "freeBuffers", 5);

        {
            TRACE_COUNTER1("app", "freeBuffers", 4);
            TRACE_EVENT0("app", "Something::useful");
            {
                TRACE_COUNTER1("app", "freeBuffers", 3);
                TRACE_EVENT0("app", "Something::else");
                usleep(4 * 1000);

            }
            TRACE_COUNTER1("app", "freeBuffers", 4);
            usleep(2 * 1000);
        }
        TRACE_COUNTER1("app", "freeBuffers", 5);
    }

    TRACE_EVENT0("app", "FiddlingBuffers");
    for (int i = 0; i < 1000; ++i) {
        char *buf;
        asprintf(&buf, "alterBuffers-%d", i);
        TRACE_EVENT0("app", buf);
        TRACE_COUNTER1("app", "freeBuffers", rand() % 100);
    }

    systrace_deinit();
}
