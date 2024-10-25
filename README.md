# Process Management (Concurrency)

### Caeden Telfer

## Features: 
- FCFS Scheduling
- Priority scheduling with preemption
- Resource Management: Allocates and releases resources to processes
- Deadlock Detection: Identifies deadlocks

---

## Usage:
**Prerequisites:**
- GCC or any standard C compiler
- Make

**Build:** `make`

**Run:**   `./process_manager [data1] [data2] [scheduler] [time_quantum]`

- data1: Path to process spec file or "generate"
- data2: Path to resource spec file or "generate"
- scheduler: Use 0 for priority, 1 for FCFS and 2 for RR
- time_quantum: Any integer (relevant for RR scheduling)

---

## Additional Notes:
- FCFS was implemented instead of RR scheduling, and so any call to schedule_RR will be redirected to the FCFS implementation
- Deadlocks are detected but not resolved. If a deadlock is detected, the program terminates.
- Uncomment debug flags '-DDEBUG_MNGR' and '-DDEBUG_LOADER' in the Makefile for a comprehensive output of process scheduling

---

## References:
- Parallel Programming by Peter Pacheco