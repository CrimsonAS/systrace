#include "CSystrace.h"
#include <unistd.h>

int main(int argc, char **argv) 
{
    systrace_init();
    CSystraceEvent ev0("app", "main");
    {

        systrace_record_counter("app", "freeBuffers", 5);

        {
            systrace_record_counter("app", "freeBuffers", 4);
            CSystraceEvent ev1("app", "Something::useful");
            {
                systrace_record_counter("app", "freeBuffers", 3);
                CSystraceEvent ev2("app", "Something::else");
                usleep(4 * 1000);

            }
            systrace_record_counter("app", "freeBuffers", 4);
            usleep(2 * 1000);
        }
        systrace_record_counter("app", "freeBuffers", 5);
    }

    CSystraceEvent ev2("app", "FiddlingBuffers");
    for (int i = 0; i < 1000; ++i) {
        char *buf;
        asprintf(&buf, "alterBuffers-%d", i);
        CSystraceEvent ev2("app", buf);
        systrace_record_counter("app", "freeBuffers", rand() % 100);
    }

    systrace_deinit();
}
