#include "CSystrace.h"
#include <unistd.h>

int main(int argc, char **argv) 
{
    {
        systrace_init();

        systrace_record_counter("app", "freeBuffers", 5);

        CSystraceEvent ev0(CSystraceEvent::fromRawData("app", "main"));
        {
            systrace_record_counter("app", "freeBuffers", 4);
            CSystraceEvent ev1(CSystraceEvent::fromRawData("app", "Something::useful"));
            {
                systrace_record_counter("app", "freeBuffers", 3);
                CSystraceEvent ev2(CSystraceEvent::fromRawData("app", "Something::else"));
                usleep(4 * 1000);

            }
            systrace_record_counter("app", "freeBuffers", 4);
            usleep(2 * 1000);
        }
        systrace_record_counter("app", "freeBuffers", 5);
    }

    for (int i = 0; i < 1000; ++i) {
        CSystraceEvent ev2(CSystraceEvent::fromRawData("app", "alterBuffers"));
        systrace_record_counter("app", "freeBuffers", rand() % 100);
    }

    systrace_deinit();
}
