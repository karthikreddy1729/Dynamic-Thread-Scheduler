/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scheduler.c
 */

#undef _FORTIFY_SOURCE

#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include "system.h"
#include "scheduler.h"

enum ThreadStatus
{
    STATUS_NEW,
    STATUS_SLEEPING,
    STATUS_RUNNING,
    STATUS_EXIT
};

struct Thread
{
    void (*fnc)(void *arg);
    void *arg;
    enum ThreadStatus status;
    char *stack;
    char *stack_start;
    jmp_buf jump_buf;
    struct Thread *next;
};

static struct Thread *current_thread = NULL;
static int activeThreads = 0;
static struct Thread *thread_list = NULL;
static struct Thread *thread_list_tail = NULL;
static jmp_buf main_env;

static int autoContextSwitchInterval = 1;

void alarm_handler()
{
    scheduler_yield();
}

int scheduler_create(scheduler_fnc_t fnc, void *arg)
{
    struct Thread *thread;

    thread = (struct Thread *)malloc(sizeof(struct Thread));
    if (thread == NULL)
    {
        printf("Allocating memory for the new thread got failed");
        return -1;
    }
    thread->stack_start = (char *)malloc(5 * page_size());
    if (thread->stack_start == NULL)
    {
        free(thread);
        return -1;
    }
    thread->stack = memory_align(thread->stack_start, page_size());
    thread->stack = (void *)((size_t)thread->stack + (4 * page_size()));
    if (thread->stack == NULL)
    {
        free(thread);
        return -1;
    }
    thread->fnc = fnc;
    thread->arg = arg;
    thread->status = STATUS_NEW;
    if (thread_list == NULL)
    {
        thread_list = thread;
        thread_list_tail = thread;
        current_thread = thread;
    }
    else
    {
        thread_list_tail->next = thread;
        thread_list_tail = thread;
    }
    activeThreads++;
    return 0;
}

void scheduler_yield()
{
    if (current_thread != NULL)
    {
        if (setjmp(current_thread->jump_buf) == 0)
        {
            current_thread->status = STATUS_SLEEPING;
            current_thread = current_thread->next;
            if (current_thread == NULL)
                current_thread = thread_list;
            longjmp(main_env, 1);
        }
    }
}

void scheduler_exit()
{
    while (thread_list != NULL)
    {
        current_thread = thread_list;
        thread_list = thread_list->next;
        if (current_thread->stack_start)
        {
            free(current_thread->stack_start);
        }
        free(current_thread);
    }
    exit(0);
}

void scheduler_execute()
{
    setjmp(main_env);
    if (activeThreads == 0)
    {
        scheduler_exit();
        return;
    }
    if (signal(SIGALRM, alarm_handler) == SIG_ERR)
    {
        perror("Error setting up the signal handler");
        exit(1);
    }
    alarm(autoContextSwitchInterval);
    if (current_thread != NULL)
    {
        if (current_thread->status == STATUS_NEW)
        {
            uint64_t rsp = (uint64_t)(current_thread->stack);
            __asm__ volatile("movq %0, %%rsp" : : "g"(rsp));
            current_thread->fnc(current_thread->arg);
            current_thread->status = STATUS_EXIT;
            activeThreads--;
            longjmp(main_env, 1);
        }
        else if (current_thread->status == STATUS_SLEEPING)
        {
            current_thread->status = STATUS_RUNNING;
            longjmp(current_thread->jump_buf, 1);
        }
        else
        {
            current_thread = current_thread->next;
            if (current_thread == NULL)
                current_thread = thread_list;
            longjmp(main_env, 1);
        }
    }
}