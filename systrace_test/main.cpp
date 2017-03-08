#include "CSystrace.h"
#include <unistd.h>

int main(int argc, char **) 
{
    systrace_init();
    TRACE_EVENT_ASYNC_BEGIN0("qtgui::kernel", "asyncTest", &argc);
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
    for (int i = 0; i < 100; ++i) {
        char *buf;
        asprintf(&buf, "alterBuffers-%d", i);
        TRACE_EVENT0("app", buf);
        TRACE_COUNTER1("app", "freeBuffers", rand() % 100);
    }

    TRACE_EVENT_ASYNC_END0("qtgui::kernel", "asyncTest", &argc);
    systrace_deinit();
}
