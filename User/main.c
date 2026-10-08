/// @file  main.c
/// @brief APP main entry.

#include "main.h"
#include "bsp.h"
#include "task.h"
#include "stdbool.h"
#include "errno.h"
/// @brief Application entry point.
/// @param None
/// @return This function does not return during normal operation.
error_t main(void)
{
    bsp_init();

    /* Loop through current range. */
    while (true)
    {
        (void)task_on_loop(&g_task_handler);
    }
}
