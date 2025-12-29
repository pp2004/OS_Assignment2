# Port Management System (Scheduler)

## 📌 Description

The Port Management System simulates ship scheduling at a cargo port using IPC mechanisms like shared memory and message queues. It manages ship docking, cargo handling, emergency prioritization, and undocking coordination through solver processes.

## 🛠️ Features

- Handles incoming & outgoing ships (regular and emergency)
- Assigns docks based on ship category and availability
- Allocates cranes based on cargo weight and crane capacity
- Coordinates cargo loading/unloading at each timestep
- Ensures emergency ships are serviced with priority
- Uses solver processes to guess radio frequency strings for undocking
- Validates all actions with a provided `validation.out` module

## 📁 Directory Structure

```
PortManagementSystem/
├── scheduler.c           # Your scheduler implementation
├── scheduler.out         # Compiled executable
├── validation.out        # Provided validator binary
├── testcase_1/
│   └── input.txt         # Input for testcase 1
├── testcase_2/
│   └── input.txt
...
└── testcase_6/
    └── input.txt
```

## 📦 Dependencies

- Ubuntu Linux (22.04 or 24.04 recommended)
- GCC (for compilation)
- POSIX-compliant IPC system calls

## ⚙️ Compilation

```bash
gcc scheduler.c -o scheduler.out
chmod +x validation.out
```

## 🚀 Running a Testcase

Open two terminals in the same directory.

**Terminal 1:**
```bash
./validation.out <X>     # e.g., ./validation.out 1
```

**Terminal 2:**
```bash
./scheduler.out <X>      # Same testcase number
```

> Always run the validator first.

## 📚 OS Concepts Used

- Shared Memory: For transferring ship requests between validator and scheduler
- Message Queues: For communication of actions (dock, cargo, undock)
- Process Synchronization: Handling timestep updates and resource allocation
- Scheduling: Handling incoming, emergency, and outgoing ships based on constraints

## 📖 Project Flow Summary

1. Parse input.txt to set up dock capacities and IPC keys
2. Establish shared memory and message queue connections
3. In each timestep:
   - Read new ship requests from shared memory
   - Dock ships based on availability and rules
   - Load/unload cargo using cranes
   - Guess auth string to undock completed ships
   - Signal end of timestep to validator
4. Terminate when validator sets `isFinished = 1`

## 👨‍🔧 Author Instructions

Do NOT access any file other than `input.txt`.  
Do NOT hardcode keys.  
Do NOT use `goto`.  
Ensure error handling for all system calls.

## ✅ Evaluation Criteria

- Functional correctness (passes all 6 testcases)
- Timesteps <= threshold per test
- Real-time execution <= 6 minutes per test
- Follows all IPC and synchronization rules
