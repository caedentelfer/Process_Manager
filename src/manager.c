/**
 * @brief This file contains the implementation of a process manager that
 *       schedules processes using different scheduling algorithms.
 *       The manager is responsible for scheduling processes, executing
 *       their instructions, and managing the resources they require.
 * 
 * @mainpage Process Simulation
 *
 * @author CJ Telfer
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "proc_structs.h"
#include "proc_syntax.h"
#include "logger.h"
#include "manager.h"

#define LOWEST_PRIORITY -1

int num_processes = 0;

/**
 * The queues as required by the spec
 */
static pcb_queue_t terminatedq;
static pcb_queue_t waitingq;
static pcb_queue_t readyq;
static bool_t readyq_updated;

void schedule_fcfs();
void schedule_rr(int quantum);
void schedule_pri_w_pre();
bool_t higher_priority(int, int);

void execute_instr(pcb_t *proc, instr_t *instr);
void request_resource(pcb_t *proc, instr_t *instr);
void release_resource(pcb_t *proc, instr_t *instr);
bool_t acquire_resource(pcb_t *proc, char *resource_name);

bool_t check_for_new_arrivals();
void move_proc_to_wq(pcb_t *pcb, char *resource_name);
void move_waiting_pcbs_to_rq(char *resource_name);
void move_proc_to_rq(pcb_t *pcb);
void move_proc_to_tq(pcb_t *pcb);
void enqueue_pcb(pcb_t *proc, pcb_queue_t *queue);
pcb_t *dequeue_pcb(pcb_queue_t *queue);

char *get_init_data(int num_args, char **argv);
char *get_data(int num_args, char **argv);
int get_algo(int num_args, char **argv);
int get_time_quantum(int num_args, char **argv);
void print_args(char *data1, char *data2, int sched, int tq);

void print_avail_resources(void);
void print_alloc_resources(pcb_t *proc);
void print_queue(pcb_queue_t queue, char *msg);
void print_running(pcb_t *proc, char *msg);
void print_instructions(instr_t *instr);

/* utility functions */
void mark_resource_as_available(char *resource_name);
bool_t is_waiting_for_resource(pcb_t *pcb, char *resource_name);
pcb_t *find_highest_priority_proc();
void remove_proc_from_readyq(pcb_t *proc);
void move_waiting_to_ready_based_on_resources();
bool_t is_resource_available(const char *resource_name);
bool_t is_resource_available_for_process(pcb_t *process);
bool_t check_deadlock();
pcb_t *find_resource_holder(char *resource_name);
pcb_t *find_holder_of_resource(char *resource_name);

/**
 * @brief Main function, initialises structures and variables
 *        starts process scheduling
*/
int main(int argc, char **argv)
{
    char *data1 = get_init_data(argc, argv);
    char *data2 = get_data(argc, argv);
    int scheduler = get_algo(argc, argv);
    int time_quantum = get_time_quantum(argc, argv);
    print_args(data1, data2, scheduler, time_quantum);

    pcb_t *initial_procs = NULL;
    if (strcmp(data1, "generate") == 0) {
#ifdef DEBUG_MNGR
        printf("****Generate processes and initialise the system\n");
#endif
        initial_procs = init_loader_from_generator();
    } else {
#ifdef DEBUG_MNGR
        printf("Parse process files and initialise the system: %s, %s \n", data1, data2);
#endif
        initial_procs = init_loader_from_files(data1, data2);
    }

    /* schedule the processes */
    if (initial_procs)  {
        num_processes = get_num_procs();
        init_queues(initial_procs);
#ifdef DEBUG_MNGR
        printf("****Scheduling processes*****\n");
#endif
        schedule_processes(scheduler, time_quantum);
        dealloc_data_structures();
    } else {
        printf("Error: no processes to schedule\n");
    }

    return EXIT_SUCCESS;
}

/**
 * @brief The linked list of loaded processes is moved to the readyqueue.
 *        The waiting and terminated queues are intialised to empty
 * @param cur_pcb: a pointer to the linked list of loaded processes
 */
void init_queues(pcb_t *cur_pcb)
{
    readyq.first = cur_pcb;
    for (cur_pcb = readyq.first; cur_pcb->next != NULL; cur_pcb = cur_pcb->next);
    readyq.last = cur_pcb;
    readyq_updated = FALSE;

    waitingq.last = NULL;
    waitingq.first = NULL;
    terminatedq.last = NULL;
    terminatedq.first = NULL;

#ifdef DEBUG_MNGR
    printf("-----------------------------------");
    print_queue(readyq, "Ready");
    printf("\n-----------------------------------");
    print_queue(waitingq, "Waiting");
    printf("\n-----------------------------------");
    print_queue(terminatedq, "Terminated");
    printf("\n\n");
#endif /* DEBUG_MNGR */
}

/**
 * @brief Schedules each instruction of each process
 *
 * @param type The scheduling algorithm to use
 * @param quantum The time quantum for the RR algorithm, if used.
 */
void schedule_processes(schedule_t sched_type, int quantum)
{
    /* Uses FCFS and not RR scheduling - RR redirects to FCFS */
    switch (sched_type) {
    case PRIOR:
        schedule_pri_w_pre();
        break;
    case RR:
        /* schedule_rr(quantum); -> does not get called*/
        schedule_fcfs();
        break;
    case FCFS:
        schedule_fcfs();
        break;
    default:
        break;
    }
}

/**
 * Schedules processes using priority scheduling with preemption 
*/
void schedule_pri_w_pre()
{
    pcb_t *current_process = NULL;
    pcb_t *highest_priority_proc;

    while (readyq.first != NULL || waitingq.first != NULL || current_process != NULL)  {
        /* Attempt to select the highest-priority process if there's no current process */
        if (!current_process) {
            current_process = find_highest_priority_proc();
            if (current_process)  {
                remove_proc_from_readyq(current_process);
                current_process->state = RUNNING;
            }
        }

        /* Execute the next instruction if the current process is running */
        if (current_process && current_process->state == RUNNING) {
            execute_instr(current_process, current_process->next_instruction);

            /* After executing an instruction, check for new arrivals */
            bool_t new_arrival = check_for_new_arrivals();

            if (current_process->next_instruction) {
                /* don't increment the instruction if a process is returning from the waitq*/
                if (current_process->state == RUNNING) {
                    current_process->next_instruction = current_process->next_instruction->next;
                }
            }  else  {
                // Process has no more instructions, move it to terminated queue
                current_process->state = TERMINATED;
                move_proc_to_tq(current_process);
                current_process = NULL;
            }

            // If a new process has arrived, check if it should preempt the current process
            if (new_arrival) {
                highest_priority_proc = find_highest_priority_proc();
                if (highest_priority_proc && (current_process == NULL 
                    || higher_priority(highest_priority_proc->priority, current_process->priority))) {
                    if (current_process)  {
                        current_process->state = READY;
                        move_proc_to_rq(current_process);
                    }
                    current_process = highest_priority_proc;
                    remove_proc_from_readyq(current_process);
                    current_process->state = RUNNING;
                }
            }

            highest_priority_proc = find_highest_priority_proc();
            if (highest_priority_proc && (current_process == NULL 
                || higher_priority(highest_priority_proc->priority, current_process->priority))) {

                if (current_process) {
                    current_process->state = READY;
                    move_proc_to_rq(current_process);
                }
                current_process = highest_priority_proc;
                remove_proc_from_readyq(current_process);
                current_process->state = RUNNING;
            }
        }  else if (current_process && current_process->state == WAITING) {
            /* If the current process is waiting, check for a new process to run */
            current_process = NULL;
        }

        /* there are no processes left to schedule */
        if (!current_process && readyq.first == NULL && waitingq.first == NULL && !check_for_new_arrivals()) {
            break;
        }

        /* Check for deadlock when readyq is empty */
        bool_t deadlock = check_deadlock();
        if (deadlock)  {
            break;
            /* resolve_deadlock(current_process); */
        }
    }
}

/**
 * Schedules processes using FCFS scheduling
*/
void schedule_fcfs()
{
    /* While there are processes in the ready queue */
    while (readyq.first != NULL) {
        pcb_t *current_process = dequeue_pcb(&readyq);

        if (current_process == NULL) {
            break;
        }

        current_process->state = RUNNING;

        /* Execute all instructions of the current process */
        while (current_process->next_instruction != NULL)  {
            execute_instr(current_process, current_process->next_instruction);
            check_for_new_arrivals();

            /* If the process is waiting for a resource, break the execution loop */
            if (current_process->state == WAITING)  {
                move_proc_to_wq(current_process, current_process->next_instruction->resource_name);
                break;
            }

            /* Move to the next instruction if not waiting */
            current_process->next_instruction = current_process->next_instruction->next;
        }

        /* If the process has completed all its instructions, move it to the terminated queue */
        if (current_process->next_instruction == NULL && current_process->state != WAITING) {
            move_proc_to_tq(current_process);
        }
    }

}

/**
 * Schedules processes using the Round-Robin scheduler.
 *
 * @param[in] quantum time quantum
 */
void schedule_rr(int quantum)
{
    /* RR option not chosen, FCFS has been implemented above */
}

/**
 * Executes a process instruction.
 *
 * @param[in] pcb
 *     processs for which to execute the instruction
 * @param[in] instr
 *     instruction to execute
 */
void execute_instr(pcb_t *pcb, instr_t *instr)
{
    if (instr != NULL) {
        switch (instr->type) {
        case REQ_OP:
            request_resource(pcb, instr);
            break;
        case REL_OP:
            release_resource(pcb, instr);
            /* After releasing a resource, check if any waiting processes can be moved to the ready queue */
            move_waiting_to_ready_based_on_resources();
            break;
        default:
            break;
        }
    } else {
        printf("Error: No instruction to execute\n");
    }

#ifdef DEBUG_MNGR
    printf("-----------------------------------");
    print_running(pcb, "Running");
    printf("\n-----------------------------------");
    print_queue(readyq, "Ready");
    printf("\n-----------------------------------");
    print_queue(waitingq, "Waiting");
    printf("\n-----------------------------------");
    print_queue(terminatedq, "Terminated");
    printf("\n");
#endif
}

/**
 * @brief Handles the request resource instruction.
 *
 * Executes the request instruction for the process. The function loops
 * through the list of resources and acquires the resource if it is available.
 * If the resource is not available the process is moved to the waiting queue
 *
 * @param current The current process for which the resource must be acquired.
 * @param instruct The request instruction
 */
void request_resource(pcb_t *cur_pcb, instr_t *instr)
{
    resource_t *resource = get_available_resources();
    bool_t found = FALSE;

    while (resource != NULL) {
        if (strcmp(resource->name, instr->resource_name) == 0) {
            if (resource->available == YES) {
                found = TRUE;
                resource->available = NO; /* Mark as unavailable */

                /* Add resource to process's list of resources */
                resource_t *new_resource = (resource_t *)malloc(sizeof(resource_t));
                if (new_resource == NULL) {
                    fprintf(stderr, "Memory allocation failed for new resource\n");
                    exit(EXIT_FAILURE);
                }

                *new_resource = *resource; /* copy resource data */
                new_resource->next = cur_pcb->resources;
                cur_pcb->resources = new_resource;

                log_request_acquired(cur_pcb->process_in_mem->name, instr->resource_name);
                break;
            }
        }
        resource = resource->next;
    }

    if (!found) {
        /* Move process to waiting queue if resource is not found or unavailable */
        move_proc_to_wq(cur_pcb, instr->resource_name);
    }
}

/**
 * @brief Acquires a resource for a process.
 *
 * @param[in] process
 *     process for which to acquire the resource
 * @param[in] resource
 *     resource name
 * @return TRUE if the resource was successfully acquire_resource; FALSE otherwise
 */
bool_t acquire_resource(pcb_t *cur_pcb, char *resource_name)
{
    if (cur_pcb == NULL || resource_name == NULL) {
        return FALSE;
    }

    resource_t *resource = get_available_resources();
    while (resource != NULL)  {
        if (strcmp(resource->name, resource_name) == 0 && resource->available == YES) {
            /* Mark the resource as unavailable */
            resource->available = NO;

            /* Create a new resource node for the process's resources list */
            resource_t *new_resource = (resource_t *)malloc(sizeof(resource_t));
            if (new_resource == NULL) {
                fprintf(stderr, "Error: Failed to allocate memory for new resource\n");
                return FALSE;
            }

            /* Copy the resource details */
            *new_resource = *resource;
            new_resource->next = cur_pcb->resources;
            cur_pcb->resources = new_resource;

            return TRUE; /* Resource successfully acquired */
        }
        resource = resource->next;
    }

    return FALSE; /* Resource not found or not available */
}

/**
 * @brief Handles the release resource instruction.
 *
 * Executes the release instruction for the process. If the resource can be
 * released the process is ready for next execution. If the resource can not
 * be released the process waits
 *
 * @param current The process which releases the resource.
 * @param instruct The instruction to release the resource.
 */
void release_resource(pcb_t *pcb, instr_t *instr)
{
    resource_t *prev = NULL;
    resource_t *cur = pcb->resources;
    bool_t found = FALSE;

    while (cur != NULL) {
        if (strcmp(cur->name, instr->resource_name) == 0) {
            found = TRUE;
            if (prev != NULL) {
                prev->next = cur->next;
            } else {
                pcb->resources = cur->next;
            }

            /* Mark the resource as available again */
            mark_resource_as_available(instr->resource_name);

            log_release_released(pcb->process_in_mem->name, instr->resource_name);
            free(cur); /* Free the resource node */
            break;
        }
        prev = cur;
        cur = cur->next;
    }

    if (!found) {
        log_release_error(pcb->process_in_mem->name, instr->resource_name);
    } else {
        /* Check waiting queue for processes waiting for this resource */
        move_waiting_pcbs_to_rq(instr->resource_name);
    }
}

/**
 * Add new process <code>pcb</code> to ready queue
 */
bool_t check_for_new_arrivals()
{
    bool_t newProcessAdded = FALSE;
    pcb_t *new_pcb = get_new_pcb();

    if (new_pcb) {
        printf("New process arriving: %s\n", new_pcb->process_in_mem->name);
        move_proc_to_rq(new_pcb);
        newProcessAdded = TRUE;
    }

    return newProcessAdded;
}

/**
 * @brief Move process <code>pcb</code> to the ready queue
 *
 * @param[in] pcb
 */
void move_proc_to_rq(pcb_t *pcb)
{
    if (pcb == NULL) return;

    /* Update process state */
    pcb->state = READY;

    enqueue_pcb(pcb, &readyq);
    log_request_ready(pcb->process_in_mem->name);
}

/**
 * Move process <code>pcb</code> to waiting queue
 */
void move_proc_to_wq(pcb_t *pcb, char *resource_name)
{
    if (pcb == NULL) return;

    /* Update process state */
    pcb->state = WAITING;

    enqueue_pcb(pcb, &waitingq);
    log_request_waiting(pcb->process_in_mem->name, resource_name);
}

/**
 * Move process <code>pcb</code> to terminated queue
 *
 * @param[in] pcb
 */
void move_proc_to_tq(pcb_t *pcb)
{
    if (pcb == NULL) return;

    /* Update process state */
    pcb->state = TERMINATED;

    enqueue_pcb(pcb, &terminatedq);
    log_terminated(pcb->process_in_mem->name);
}

/**
 * Moves all processes waiting for resource <code>resource_name</code>
 * from the waiting queue to the readyq queue.
 *
 * @param[in]   resource
 *     resource name
 */
void move_waiting_pcbs_to_rq(char *resource_name)
{
    pcb_t *prev = NULL;
    pcb_t *current = waitingq.first;
    pcb_t *temp;

    while (current != NULL)
    {
        /* Check if the current process is waiting for the given resource */
        if (is_waiting_for_resource(current, resource_name)) {
            temp = current->next;

            /* Remove current process from waiting queue */
            if (prev != NULL) prev->next = current->next;
            else  waitingq.first = current->next;
            if (waitingq.last == current) waitingq.last = prev;

            /* Move current process to ready queue */
            current->next = NULL;
            move_proc_to_rq(current);

            current = temp; /* Continue with the next process */
        } else{
            prev = current;
            current = current->next;
        }
    }
}

/**
 * Enqueues process <code>pcb</code> to <code>queue</code>.
 *
 * @param[in] process
 *     process to enqueue
 * @param[in] queue
 *     queue to which the process must be enqueued
 */
void enqueue_pcb(pcb_t *pcb, pcb_queue_t *queue)
{
    if (pcb == NULL || queue == NULL) return;

    /* Initialize the next pointer of the process to ensure it's the last in the queue */
    pcb->next = NULL;

    /* If the queue is empty, the new process is both the first and last element */
    if (queue->first == NULL) {
        queue->first = pcb;
        queue->last = pcb;
    } else {
        /* If the queue is not empty, append the process to the end and update the last pointer */
        queue->last->next = pcb;
        queue->last = pcb;
    }
}

/**
 * Dequeues a process from queue <code>queue</code>.
 *
 * @param[in] queue
 *     queue from which to dequeue a process
 * @return dequeued process
 */
pcb_t *dequeue_pcb(pcb_queue_t *queue)
{
    if (queue == NULL || queue->first == NULL)
        return NULL;

    /* Store the first process in the queue */
    pcb_t *proc = queue->first;

    queue->first = queue->first->next;

    /* If the queue becomes empty, make sure the last pointer is also NULL */
    if (queue->first == NULL) {
        queue->last = NULL;
    }

    proc->next = NULL;
    return proc;
}

/** @brief Return TRUE if pri1 has a higher priority than pri2
 *         where higher values == higher priorities
 *
 * @param[in] pri1
 *     priority value 1
 * @param[in] pri2
 *     priority value 2
 */
bool_t higher_priority(int pri1, int pri2)
{
    return pri1 > pri2 ? TRUE : FALSE;
}

/**
 * @brief Inspect the waiting queue and detects deadlock
 */
struct pcb_t *detect_deadlock()
{
    log_deadlock_detected(); 

    return NULL;
}

/**
 * @brief Releases a processes' resources and sets it to its first instruction.
 *
 * Generates release instructions for each of the processes' resoures and forces
 * it to execute those instructions.
 *
 * @param pcb The process chosen to be reset and release all of its resources.
 *
 */
void resolve_deadlock(struct pcb_t *pcb)
{
    /* Resolve deadlocks in pcb_t */
    printf("Function resolve_deadlock not implemented\n");
}

/**
 * @brief Deallocates the queues
 */
void free_manager(void)
{
#ifdef DEBUG_MNGR
    print_queue(readyq, "Ready");
    print_queue(waitingq, "Waiting");
    print_queue(terminatedq, "Terminated");
#endif

#ifdef DEBUG_MNGR
    printf("\nFreeing the queues...\n");
#endif
    dealloc_pcb_list(readyq.first);
    dealloc_pcb_list(waitingq.first);
    dealloc_pcb_list(terminatedq.first);
}

/**
 * @brief Retrieves the name of a process file or the codename "generator" from the list of arguments
 */
char *get_init_data(int num_args, char **argv)
{
    char *data_origin = "generate";
    if (num_args > 1) return argv[1];
    else return data_origin;
}

/**
 * @brief Retrieves the name of a process file or the codename "generator" from the list of arguments
 */
char *get_data(int num_args, char **argv)
{
    char *data_origin = "generate";
    if (num_args > 2) return argv[2];
    else return data_origin;
}

/**
 * @brief Retrieves the scheduler algorithm type from the list of arguments
 */
int get_algo(int num_args, char **argv)
{
    if (num_args > 3)  return atoi(argv[3]);
    else return 1;
}

/**
 * @brief Retrieves the time quantum from the list of arguments
 */
int get_time_quantum(int num_args, char **argv)
{
    if (num_args > 4)  return atoi(argv[4]);
    else return 1;
}

/**
 * @brief Print the arguments of the program
 */
void print_args(char *data1, char *data2, int sched, int tq)
{
    printf("Arguments: data1 = %s, data2 = %s, scheduler = %s,  time quantum = %d\n", data1, data2, (sched == 0) ? "priority" : "RR", tq);
}

/**
 * @brief Print the names of the global resources available in the system in linked list order
 */
void print_avail_resources(void)
{
    struct resource_t *resource;

    printf("Available:");
    for (resource = get_available_resources(); resource != NULL; resource = resource->next) {
        if (resource->available == YES)  {
            printf(" %s", resource->name);
        }
    }
    printf(" ");
}

/**
 * @brief Print the names of the resources allocated to <code>process</code> in linked list order.
 */
void print_alloc_resources(pcb_t *proc)
{
    struct resource_t *resource;

    if (proc)  {
        printf("Allocated to %s:", proc->process_in_mem->name);
        for (resource = proc->resources; resource != NULL; resource = resource->next) {
            printf(" %s", resource->name);
        }
        printf(" ");
    }
}

/**
 * @brief Print <code>msg</code> and the names of the processes in <code>queue</code> in linked list order.
 */
void print_queue(pcb_queue_t queue, char *msg)
{
    pcb_t *proc = queue.first;

    printf("%s:", msg);
    while (proc != NULL) {
        printf(" %s", proc->process_in_mem->name);
        proc = proc->next;
    }
    printf(" ");
}

/**
 * @brief Print <code>msg</code> and the names of the process currently running
 */
void print_running(pcb_t *proc, char *msg)
{
    printf("%s:", msg);
    if (proc != NULL) {
        printf(" %s", proc->process_in_mem->name);
    }
    printf(" ");
}

/**
 * @brief Print a linked list of instructions
 */
void print_instructions(instr_t *instr)
{
    instr_t *tmp_instr = instr;
    printf("Instructions:\n");
    while (tmp_instr != NULL)  {
        printf("Instructions NOT NULL !!!\n");
        switch (tmp_instr->type) {
        case REQ_OP:
            printf("(req %s)\n", tmp_instr->resource_name);
            break;
        case REL_OP:
            printf("(rel %s)\n", tmp_instr->resource_name);
            break;
        case SEND_OP:
            printf("(send %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
            break;
        case RECV_OP:
            printf("(recv %s %s)\n", tmp_instr->resource_name, tmp_instr->msg);
            break;
        }
        tmp_instr = tmp_instr->next;
    }
}

/**
 * @brief Marks resources as available
 * @param resource_name The name of the resource to mark as available
 */
void mark_resource_as_available(char *resource_name)
{
    resource_t *resource = get_available_resources();
    while (resource != NULL)  {
        if (strcmp(resource->name, resource_name) == 0)  {
            resource->available = YES;
            break;
        }
        resource = resource->next;
    }
}

/**
 * @brief Checks if a process is waiting for a resource
 * @param pcb The pcb structure
 * @param resource_name The name of the resource to check for
 * @return TRUE if the process is waiting for the resource, FALSE otherwise
 */
bool_t is_waiting_for_resource(pcb_t *pcb, char *resource_name)
{
    if (pcb == NULL || resource_name == NULL) {
        return FALSE;
    }

    if (pcb->state == WAITING) {
        /* Iterate through the process's instructions to find a REQ_OP for the given resource */
        instr_t *current_instr = pcb->next_instruction;
        while (current_instr != NULL) {
            if (current_instr->type == REQ_OP && strcmp(current_instr->resource_name, resource_name) == 0) {
                return TRUE; /* Found a matching request instruction */
            }
            current_instr = current_instr->next;
        }
    }

    /* No matching request found or the process is not in the waiting state */
    return FALSE;
}

/**
 * @brief Finds the process with the highest priority in the ready queue
 * @return The process that holds the resource, or NULL if not found
 */
pcb_t *find_highest_priority_proc()
{
    pcb_t *current = readyq.first;
    pcb_t *highest_priority_proc = NULL;
    int highest_priority = INT_MIN;

    while (current != NULL) {
        if (current->priority > highest_priority) {
            highest_priority = current->priority;
            highest_priority_proc = current;
        }
        current = current->next;
    }

    return highest_priority_proc;
}

/**
 * @brief removes a process from the ready queue
 * @param proc The process to remove
 */
void remove_proc_from_readyq(pcb_t *proc)
{
    if (proc == NULL || readyq.first == NULL) return;

    if (readyq.first == proc) {
        /* If the process to remove is the first in the queue */
        readyq.first = proc->next;
        if (readyq.last == proc) {
            /* If it was also the last process, then the queue is now empty */
            readyq.last = NULL;
        }
    } else {
        /* If the process is not the first, find it in the queue */
        pcb_t *current = readyq.first;
        while (current != NULL && current->next != proc) {
            current = current->next;
        }

        if (current != NULL) {
            /* Remove the process from the queue */
            current->next = proc->next;
            if (readyq.last == proc)  {
                readyq.last = current;
            }
        }
    }
    proc->next = NULL;
}

/**
 * @brief Moves processes from the waiting queue to the ready queue based on resource availability
 */
void move_waiting_to_ready_based_on_resources()
{
    pcb_t **current = &waitingq.first;
    pcb_t *prev = NULL;

    while (*current != NULL) {
        pcb_t *process = *current;
        bool_t canMoveToReady = FALSE;

        if (process->next_instruction) {
            const char *requiredResource = process->next_instruction->resource_name;
            if (is_resource_available(requiredResource)) {
                canMoveToReady = TRUE;
            }
        }

        if (canMoveToReady) {
            /* Remove from waiting queue */
            if (prev != NULL) {
                prev->next = process->next;
            } else {
                waitingq.first = process->next;
            }

            /* If it was the last process in the waiting queue */
            if (waitingq.last == process) {
                waitingq.last = prev;
            }

            move_proc_to_rq(process);

            /* Adjust current pointer if process was removed */
            if (prev != NULL) {
                current = &(prev->next);
            } else {
                current = &waitingq.first;
            }
        } else {
            prev = *current;
            current = &((*current)->next);
        }
    }
}

/**
 * @brief Checks if a resource is available
 * @param resource_name The name of the resource to check
 * @return TRUE if the resource is available, FALSE otherwise
 */
bool_t is_resource_available(const char *resource_name)
{
    resource_t *resource = get_available_resources();
    while (resource != NULL) {
        if (strcmp(resource->name, resource_name) == 0) {
            return resource->available == YES ? TRUE : FALSE;
        }
        resource = resource->next;
    }
    return FALSE;
}

/**
 * @brief Checks if a resource is available for a process
 * @param process The process to check
 * @return TRUE if the resource is available, FALSE otherwise
 */
bool_t is_resource_available_for_process(pcb_t *process)
{
    if (process == NULL || process->next_instruction == NULL) {
        return FALSE;
    }
    if (process->next_instruction->type == REQ_OP) {
        return is_resource_available(process->next_instruction->resource_name);
    }
    return FALSE;
}

/**
 * @brief Checks for deadlock
 * @return TRUE if a deadlock is detected, FALSE otherwise
 */
bool_t check_deadlock()
{
    if (readyq.first == NULL && waitingq.first != NULL) {
        detect_deadlock();
        return TRUE;
    }

    return FALSE;
}