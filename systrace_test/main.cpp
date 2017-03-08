#include "CSystrace.h"

int main(int argc, char **argv) 
{
    {
        systrace_init();

        CSystraceEvent ev0(CSystraceEvent::fromRawData("app", "main"));
        CSystraceEvent ev1(CSystraceEvent::fromRawData("app", "Something::useful"));
        CSystraceEvent ev2(CSystraceEvent::fromRawData("app", "Something::else"));
    }

    systrace_deinit();
}
