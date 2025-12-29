#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <errno.h>
#include <string.h>
#include <sys/shm.h>
#include <pthread.h>
#define MAX_DOCKS 30
#define MAX_AUTH_STRING_LEN 100
#define MAX_NEW_REQUESTS 100
#define MAX_CARGO_COUNT 200
#define PERMS 0666

typedef struct ShipRequest
{
    int shipId;
    int timestep;
    int category;
    int direction;
    int emergency;
    int waitingTime; // after docking waiting time does not hold any importance we'll use it as dock_stauts (allocated (-1) or not )
    int numCargo;
    int cargo[MAX_CARGO_COUNT];
} ShipRequest;

typedef struct MainSharedMemory
{
    char authStrings[MAX_DOCKS][MAX_AUTH_STRING_LEN];
    ShipRequest newShipRequests[MAX_NEW_REQUESTS];
} MainSharedMemory;

typedef struct MessageStruct
{
    long mtype;
    int timestep;
    int shipId;
    int direction;
    int dockId;
    int cargoId;
    int isFinished;
    union
    {
        int numShipRequests;
        int craneId;
    };
} MessageStruct;

typedef struct SolverRequest
{
    long mtype;
    int dockId;
    char authStringGuess[MAX_AUTH_STRING_LEN];
} SolverRequest;

typedef struct SolverResponse
{
    long mtype;
    int guessIsCorrect;
} SolverResponse;

typedef struct ThreadData
{
    ShipRequest ship;
    int solver_id;
    int length;
    int index;
    char *start_string;
    int capacity;

} ThreadData;

int shipPriorityComparator(const void *a, const void *b);
void swapDocks(int docks[MAX_DOCKS][3], int i, int j);
void sortDocksByCategory(int docks[MAX_DOCKS][3], int numDocks);
int allocateDock(int ship_category);
int manageCargo(ShipRequest ship, int index, int dock_cat, int dockId, int crane_capacity[]);
int undock(int length, ShipRequest ship, int n_solvers, int solver_keys[n_solvers]);
void increaseTimestamp(int mmqid);
void merge(int cargo[][2], int left, int mid, int right);
void merge_sort(int cargo[][2], int left, int right);
int binary_search(const int arr[][2], int start, int end, int target);
int removeNegativeWaitingTimeShips(ShipRequest ships[], int *count);
void generateInitialGuess(char *guess, int length);
int updateGuess(char *guess, int length);
void updateSharedMemory(int dockId, const char *authString);
void freedockwithid(int dockid);
char *undock_new(int length, int solver_id, ShipRequest ship, int index, int capacity, char *start_string);
char *getnext(const char *guessString, int length);
int updateGuess_new(char *guess, int length, int index);
// char *valid_chars_perm[8] = {"56789", "67895", "78956", "89567", "95678", "57896", "78965", "69578"};
// char *valid_end_chars_perm[8] = {"56789.", "6789.5", "789.56", "89.567", "9.5678", "5.6789", "5.6987", "67.895"};
void *thrsolver(void *arg);
// Convert a character to its numeric value
int charToValue(char c);

// Custom power function to calculate a^b
int powe(int a, int b);

// Convert a numeric value back to a character
char valueToChar(int value, int isEdgePosition);

// Convert a string to its numeric representation
long long int stringToValue(const char *str);

// Convert a numeric value back to its string representation
char *valueToString(long long int value, int length);

// Calculate the total number of possible strings
long long int calculateTotalStrings(int length);

// Function to partition strings and return start strings and capacities
void partitionStringsWithArrays(int length, int numPartitions, char ***startStrings, int **capacities);
pthread_t threads[8];

// glob vars
MainSharedMemory *shmptr;
int curr_timestamp;
ShipRequest shipReq[2000]; // assuming that max unallocated ships can be 1000 at any instant
int docktime[2000];
int lastship_index;
int dock_status[MAX_DOCKS][3];
int n_docks;
int mmqid;
int n_solvers;
int *solver_keys = NULL;
int shmid;

// comparator function to sort the ship request by waiting time (ascending) and keep the emergency ships first

// main begins :-
int main(int argc, char *argv[])
{
    // Check if testcase is provided as command line argument
    if (argc < 2)
    {
        //printf("No testcase provided\n");
        return 0;
    }

    // Get testcase value from command line argument
    int testcase = atoi(argv[1]);

    // Create path string for input file
    char path[100];
    snprintf(path, sizeof(path), "testcase%d/%s", testcase, "input.txt");

    // Open input file
    FILE *input = fopen(path, "r");

    // Error handling for input file
    if (input == NULL)
    {
        perror("Error opening the input file");
        return 1;
    }

    // Read the values form the input file provided to us in the corresponding testcase
    int SMKey, MMQKey;

    fscanf(input, "%d", &SMKey);
    fscanf(input, "%d", &MMQKey);
    fscanf(input, "%d", &n_solvers);
    solver_keys = (int *)malloc(n_solvers * sizeof(int));
    // Reading  solver message queue keys

    for (int i = 0; i < n_solvers; i++)
    {
        fscanf(input, "%d", &solver_keys[i]);
    }

    // Reading number of docks
    fscanf(input, "%d", &n_docks);

    // Reading dock categories and crane capacities
    int *dock_category = (int *)malloc(n_docks * sizeof(int));
    int **crane_capacity = (int **)malloc(n_docks * sizeof(int *));
    for (int i = 0; i < n_docks; i++)
    {
        // First number in each dock line is the dock category ie basically the number of cranes avaliable at that dock
        fscanf(input, "%d", &dock_category[i]);

        // Allocate space for crane capacities for this dock
        crane_capacity[i] = (int *)malloc(dock_category[i] * sizeof(int));

        // Read capacity for each crane in this dock
        for (int j = 0; j < dock_category[i]; j++)
        {
            fscanf(input, "%d", &crane_capacity[i][j]);
        }
    }

    // Close the file
    fclose(input);

    // Printing all the variables to see that the extraction is correct

    /*
    //printf("Shared Memory Key (SMKey): %d\n", SMKey);
    //printf("Main Message Queue Key (MMQKey): %d\n", MMQKey);
    //printf("Number of Solvers (m): %d\n", n_solvers);

    //printf("\nSolver Message Queue Keys:\n");
    for (int i = 0; i < n_solvers; i++)
    {
        //printf("Solver %d: %d\n", i, solver_keys[i]);
    }

    //printf("\nNumber of Docks (n): %d\n", n_docks);

    //printf("\nDock Information:\n");
    for (int i = 0; i < n_docks; i++)
    {
        //printf("Dock %d:\n", i);
        //printf("  Dock Category (number of cranes): %d\n", dock_category[i]);
        //printf("  Crane Capacities: ");

        for (int j = 0; j < dock_category[i]; j++)
        {
            //printf("%d ", crane_capacity[i][j]);
        }
        //printf("\n");
    }
    */

    /* Let's begin the next task that is to establish IPC communication
       the communications we need to establish are :
       1. shared memory and sheduler prog
       2. main message queue b/w validation and scheduler
       3. message queue b/w solver processes and scheduler
    */

    // establish communication between validatior and scheduler using msg que

    // //printf("%lu\n", sizeof(ShipRequest));
    // //printf("%d", MMQKey);

    //printf("dock category :");
    for (int i = 0; i < n_docks; i++)
    {
        dock_status[i][0] = i;
        dock_status[i][1] = dock_category[i];

        //printf("dock %d has cat %d \n",dock_status[i][0], dock_status[i][1]);
        // 0 means dock is avalible and 1 means unavalible

        dock_status[i][2] = 0;
    }

    sortDocksByCategory(dock_status, n_docks);
    // //printf("\n\n Docks After Sorting\n");
    // for (int i = 0; i < n_docks; i++)
    // {
    //     //printf("Dock %d: Category = %d, Status = %d\n",
    //            dock_status[i][0],
    //            dock_status[i][1],
    //            dock_status[i][2]);
    // }

    // //printf("\n");
    curr_timestamp = 1;
    lastship_index = 0;
    while (1)
    {
        // block 1 msg Mtype 1
        mmqid = msgget(MMQKey, PERMS | IPC_CREAT);
        if (mmqid == -1)
        {
            perror("Error in establishing the main memory queue");
            exit(1);
        }

        MessageStruct msg;
        // //printf("Waiting for message on mmqid: %d\n", mmqid);
        if (msgrcv(mmqid, &msg, sizeof(MessageStruct) - sizeof(long), 1, 0) == -1)
        {
            perror("err in receiving message from main msg queue");
            // exit(1);
        }

        //printf("NO of request: \"%d\"\n", msg.numShipRequests);

        // Block 2 reading the SM

        shmid = shmget(SMKey, sizeof(MainSharedMemory), PERMS | IPC_CREAT);
        if (shmid == -1)
        {
            perror("SHMID error !");
            return 1;
        }
        // //printf(" shm id %d\n", shmid);
        shmptr = (MainSharedMemory *)shmat(shmid, NULL, 0);
        if (shmptr == (void *)-1)
        {
            perror("SHM pointer ERROR");
            return 1;
        }
        // //printf("no err");
        // for (int i = 0; i < MAX_NEW_REQUESTS; i++)
        // {
        //     ShipRequest req = shmptr->newShipRequests[i];
        // }
        //     // Skip empty requests (assuming timestep 0 means empty)
        //     if (req.timestep == 0)
        //         continue;

        //     // Print the ship request details
        //     // //printf("Ship %d: timestep=%d, category=%d, direction=%d, emergency=%d, waitingTime=%d, numCargo=%d, cargo=[",
        //     //        req.shipId, req.timestep, req.category, req.direction,
        //     //        req.emergency, req.waitingTime, req.numCargo);

        //     // Print cargo array
        //     for (int j = 0; j < req.numCargo; j++)
        //     {
        //         //printf("%d%s", req.cargo[j], (j < req.numCargo - 1) ? ", " : "");
        //     }
        //     //printf("]\n");
        // }
        // Dock Assignment Procedure :-

        // Creating a dock_status to check if its is free or occupied

        // alloting the dock

        ShipRequest temp_shipReq[100];
        for (int i = 0; i < msg.numShipRequests; i++)
        {
            temp_shipReq[i] = shmptr->newShipRequests[i];
            if (temp_shipReq[i].timestep != 0)
                // //printf("Ship %d: timestep=%d, category=%d, direction=%d, emergency=%d, waitingTime=%d, numCargo=%d \n",
                //        temp_shipReq[i].shipId, temp_shipReq[i].timestep, temp_shipReq[i].category, temp_shipReq[i].direction,
                //        temp_shipReq[i].emergency, temp_shipReq[i].waitingTime, temp_shipReq[i].numCargo);

                shipReq[lastship_index++] = temp_shipReq[i];
        }
        qsort(shipReq, 2000, sizeof(ShipRequest), shipPriorityComparator);
        // removeNegativeWaitingTimeShips(shipReq, &lastship_index);
        // //printf("\nServicing Order:\n");
        // for (int i = 0; i < 1000; i++)
        // {
        //     if (shipReq[i].timestep != 0)
        //     {
        //         //printf("Ship ID: %d | Emergency: %d | Direction: %d | WaitingTime: %d\n",
        //                shipReq[i].shipId,
        //                shipReq[i].emergency,
        //                shipReq[i].direction,
        //                shipReq[i].waitingTime);
        //     }
        // }

        // Allocating dock to a  ship
        //printf("\n docks status : ");
        for(int i=0; i<n_docks; i++)
        {
          //printf("id  %d  status  %d \n",dock_status[i][0], dock_status[i][2]);
          if(dock_status[i][2]>1)
          {
            //printf(" shit happened ");
            exit(0);
          }
          
        }
        for (int i = 0; i < 2000; i++)
        {
            if (shipReq[i].waitingTime < 0 && shipReq[i].timestep != 0)
            {
                int dock_id = (-shipReq[i].waitingTime) - 1;
                manageCargo(shipReq[i], i, dock_category[dock_id], dock_id, crane_capacity[dock_id]);
            }

            if (shipReq[i].waitingTime >= 0 && (shipReq[i].waitingTime >= curr_timestamp - shipReq[i].timestep || shipReq[i].direction == -1 || shipReq[i].emergency == 1) && shipReq[i].timestep != 0)
            {
                // //printf("\n");
                int allocatedDock = allocateDock(shipReq[i].category);
                if (allocatedDock != -1)
                {
                    // //printf("Allocated ship with id:%d and dir: %d Dock with id: %d\n", shipReq[i].shipId, shipReq[i].direction, allocatedDock);
                    // notify that the ship has been docked to the validator
                    MessageStruct dock_msg;
                    dock_msg.mtype = 2;
                    dock_msg.dockId = allocatedDock;
                    dock_msg.shipId = shipReq[i].shipId;
                    dock_msg.direction = shipReq[i].direction;
                    shipReq[i].waitingTime = (-1 * allocatedDock) - 1;
                    shipReq[i].timestep = -curr_timestamp;
                    // means dock allocated
                  
                    if (msgsnd(mmqid, &dock_msg, sizeof(MessageStruct) - sizeof(long), 0) == -1)
                    {
                        perror("err in sending docl_msg to main msg queue");
                        exit(1);
                    }
                }
                else
                {
                    // //printf("cannot allocate dock to ship with id:%d and dir: %d \n", shipReq[i].shipId, shipReq[i].direction);
                }
            }
        }
        for (int i = 0; i < n_docks; i++)
        {
            if (dock_status[i][2] == -1)
                dock_status[i][2] = 0; // making those docks that got free at this timestamp avalible for next
        }

        /* This code is to increase timestep */
        increaseTimestamp(mmqid);
    }
}

int shipPriorityComparator(const void *a, const void *b)
{
    ShipRequest *A = (ShipRequest *)a;
    ShipRequest *B = (ShipRequest *)b;
    // 1. Emergency ships first
    if (A->emergency != B->emergency)
        return B->emergency - A->emergency;
    // 2. Incoming (1) before Outgoing (-1)
    // Treat anything that’s not 1 as outgoing
    int dirA = (A->direction == 1) ? 1 : -1;
    int dirB = (B->direction == 1) ? 1 : -1;
    if (dirA != dirB)
        return dirB - dirA;
    // incoming (1) > outgoing (-1)
    // 3. If same direction, use waiting time (ascending)
    return ((A->waitingTime - A->timestep) - (B->waitingTime - B->timestep));
}
int removeNegativeWaitingTimeShips(ShipRequest ships[], int *count)
{
    int removed = 0;
    int i = 0;
    int newCount = 0;

    for (i = 0; i < *count; i++)
    {
        int waitingTime = ships[i].waitingTime;

        if (waitingTime >= 0)
        {
            // Keep this ship by moving it to the new position
            if (i != newCount)
            {
                ships[newCount] = ships[i];
            }
            newCount++;
        }
        else
        {
            // Skip this ship (negative waiting time)
            removed++;
        }
    }

    // Update the count to the new value
    *count = newCount;

    return removed;
}

// Swap full column data across all rows
void swapDocks(int docks[MAX_DOCKS][3], int i, int j)
{
    for (int r = 0; r < 3; r++)
    {
        int temp = docks[i][r];
        docks[i][r] = docks[j][r];
        docks[j][r] = temp;
    }
}
// Sort docks based on category (row 1)
void sortDocksByCategory(int docks[MAX_DOCKS][3], int numDocks)
{
    for (int i = 0; i < numDocks - 1; i++)
    {
        for (int j = i + 1; j < numDocks; j++)
        {
            if (docks[i][1] > docks[j][1])
            {
                swapDocks(docks, i, j);
            }
        }
    }
}
int allocateDock(int ship_category)
{
    for (int i = 0; i < n_docks; i++)
    {
        if (dock_status[i][2] == 0 && ship_category <= dock_status[i][1])
        {
            dock_status[i][2] = 1;    // Mark as occupied
            //printf("allocating dock %d with cat %d\n", dock_status[i][0], dock_status[i][1]);
            return dock_status[i][0]; // Return dock index
        }

        // else
        // {
        //     //printf("dock allocation failed ");
        // }
    }
    return -1; // No dock available
}
// this function is for unloading or loading the cargo
int manageCargo(ShipRequest ship, int index, int dock_cap, int dockId, int crane_capacity[])
{

    int ship_cargo[ship.numCargo][2];
    int sorted_crane_cap[dock_cap][2];
    int i = 1;
    // making a copy of ships cargo array
    for (int i = 0; i < ship.numCargo; i++)
    {
        ship_cargo[i][0] = ship.cargo[i]; // Copy cargo value
        ship_cargo[i][1] = i;             // Store original index
    }

    // //printf("\n");
    for (int i = 0; i < dock_cap; i++)
    {
        sorted_crane_cap[i][0] = crane_capacity[i]; // Copy cargo value
        sorted_crane_cap[i][1] = i;                 // Store original index
    }

    int ship_cargo_is_empty = 1; // Assume empty
    merge_sort(ship_cargo, 0, ship.numCargo - 1);
    merge_sort(sorted_crane_cap, 0, dock_cap - 1);
    // print after sorting
    // //printf("Ship cargo after sorting :");
    // for (int i = 0; i < ship.numCargo; i++)
    // {
    //     //printf("%d", ship_cargo[i][0]);
    // }
    // //printf("\n");
    // //printf("Sorted CraneCap : ");
    // for (int i = 0; i < dock_cap; i++)
    // {
    //     //printf("%d ", sorted_crane_cap[i][0]); // Print only the first row (cargo values)
    // }
    // //printf("\n");

    while (i--)
    {
        if (ship.waitingTime < 0) // means ship is docked
        {
            for (int i = 0; i < shipReq[index].numCargo; i++)
            {
                if (shipReq[index].cargo[i] != 0)
                {
                    ship_cargo_is_empty = 0;
                    break;
                }
            }

            if (!ship_cargo_is_empty)
            {
                for (int j = 0; j < dock_cap; j++)
                { //  //printf("searching for :%d",sorted_crane_cap[i][0]);
                    int result = binary_search(ship_cargo, 0, ship.numCargo - 1, sorted_crane_cap[j][0]);

                    // //printf("result : %d\n", result);
                    if (result != -1)
                    {
                        MessageStruct handle_cargo;
                        handle_cargo.mtype = 4;
                        handle_cargo.dockId = dockId;
                        handle_cargo.shipId = ship.shipId;
                        handle_cargo.direction = ship.direction;
                        handle_cargo.cargoId = ship_cargo[result][1];
                        handle_cargo.craneId = sorted_crane_cap[j][1];
                        ship_cargo[result][0] = 0;
                        shipReq[index].cargo[handle_cargo.cargoId] = 0;

                        // //printf("Ship cargo after unloading last cargo :");
                        // for (int i = 0; i < ship.numCargo; i++)
                        // {
                        //     //printf("%d", ship_cargo[i][0]);
                        // }
                        if (msgsnd(mmqid, &handle_cargo, sizeof(MessageStruct) - sizeof(long), 0) == -1)
                        {
                            perror("err in sending handle_cargo_msg to main msg queue");
                            exit(1);
                        }
                        else
                        {
                            // success
                            // //printf("Cargo operation: Ship ID: %d | Direction: %d | Dock ID: %d | Ship Capacity: %d | Cargo ID: %d | Crane ID: %d\n",
                            //    handle_cargo.shipId, handle_cargo.direction, handle_cargo.dockId, dock_cap, handle_cargo.cargoId, handle_cargo.craneId);
                        }
                        merge_sort(ship_cargo, 0, ship.numCargo - 1);
                    }
                }
            }

            else
            {
                if (ship.numCargo != -1)
                {
                    //printf("ship with id [%d] and dir [%d] is empty \n", ship.shipId, ship.direction);

                    // //printf("Undocking ship [%d] direction [%d] at timestamp [%d] \n", ship.shipId, ship.direction, curr_timestamp);
                    int length = (curr_timestamp - 1) - (-ship.timestep);
                    char **startStrings;
                    int *capacities;
                    if (ship.shipId == 145 && ship.direction == 1)
                    {
                        //printf(" length is %d \n", length);
                        //printf("strings should be %d \n", 25 * powe(6, length - 2));
                        int totalcap = 0;
                        for (int i = 0; i < n_solvers; i++)
                        {
                            totalcap++;
                        }
                        //printf("total cap is %d \n", totalcap);
                    }

                    // Get partition information in array form
                    partitionStringsWithArrays(length, n_solvers, &startStrings, &capacities);
                    // undock(length,ship,n_solvers,solver_keys);
                    for (int i = 0; i < n_solvers; i++)
                    {
                        ThreadData *data = malloc(sizeof(ThreadData));
                        data->ship = ship;
                        data->solver_id = msgget(solver_keys[i], PERMS | IPC_CREAT);
                        data->length = length;
                        data->index = i;
                        data->capacity = capacities[i];
                        data->start_string = startStrings[i];
                        pthread_create(&threads[i], NULL, thrsolver, (void *)data);
                    }
                    char *final_result = NULL;
                    for (int i = 0; i < n_solvers; i++)
                    {
                        void *res;
                        // //printf("joining thread %d\n", i);
                        pthread_join(threads[i], &res);
                        if (res != NULL)
                        {
                            final_result = (char *)res;
                            // break;
                        }
                        // //printf(" received from thread %d : %s\n", i, (char *)res);
                    }
                    //printf("putting final result in shared memory : %s for ship %d \n", final_result, ship.shipId);
                    updateSharedMemory(dockId, final_result);

                    // Step 4: Send message to validation
                    // int validation_queue = msgget(, PERMS);
                    // if (validation_queue == -1) {
                    //     perror("Error accessing validation message queue");
                    //     return -1;
                    // }

                    MessageStruct validationMsg;
                    memset(&validationMsg, 0, sizeof(validationMsg));
                    validationMsg.mtype = 3;
                    validationMsg.dockId = dockId;
                    validationMsg.shipId = ship.shipId;
                    validationMsg.direction = ship.direction;

                    if (msgsnd(mmqid, &validationMsg, sizeof(validationMsg) - sizeof(long), 0) == -1)
                    {
                        perror("Error sending message to validation");
                        return -1;
                    }
                    else
                    {
                        //printf("notified validation about undocking");
                    }
                }
                shipReq[index].numCargo = -1;
            }
        }
    }
}
char *undock_new(int length, int solver_id, ShipRequest ship, int index, int capacity, char *start_string)
{
    int dockId = (-ship.waitingTime) - 1;
    SolverRequest request;
    memset(&request, 0, sizeof(request));
    request.mtype = 1; // Initial communication message type
    request.dockId = dockId;
    // authStringGuess field should be ignored in this message

    if (msgsnd(solver_id, &request, sizeof(request) - sizeof(long), 0) == -1)
    {
        perror("Error sending initial message to solver");
        // return -1;
    }
    else
    {
        // //printf("initial handshake sent to solver %d\n", index);
    }
    char *guessString = start_string;
    int guessCorrect = 0;
    // //printf(" thread no %d\n", index);
    // generateInitialGuess(guessString, length);
    // //printf("Initial guess: %s\n", guessString);
    int cap = capacity;
    while (!guessCorrect && cap)
    {
        // //printf(" string is : %s\n", guessString);
        SolverRequest request;
        memset(&request, 0, sizeof(request));
        request.mtype = 2; // Guess message type
        request.dockId = dockId;
        strcpy(request.authStringGuess, guessString);

        if (msgsnd(solver_id, &request, sizeof(request) - sizeof(long), 0) == -1)
        {
            perror("Error sending guess to solver");
            // return -1;
        }
        else
        {
            // //printf("guess sent to solver %d\n", index);
        }
        // Wait for response
        SolverResponse response;
        if (msgrcv(solver_id, &response, sizeof(response) - sizeof(long), 3, 0) == -1)
        {
            perror("Error receiving response from solver");
            // return -1;
        }
        else
        {
            // //printf("response received from solver %d\n", index);
        }

        // Process response
        switch (response.guessIsCorrect)
        {
        case 0:
            // Guess is incorrect, try a new guess
            // //printf(" old guess for thr %d is : %s\n", index, guessString);
            guessString = getnext(guessString, length);
            // //printf(" new guess for thr %d is : %s\n", index, guessString);
            cap--;
            // //printf("wrong pass %s", guessString);
            break;
        case 1:
            // Guess is correct!
            guessCorrect = 1;
            // //printf("\ncorrect pass ! %s %d  with ship %d\n", guessString, index, ship.shipId);
            return guessString;
            break;
        case -1:
            // //printf("Dock not set, cargo not moved, or no ship at dock with %s\n",guessString);
            // Either dock not set, cargo not moved, or no ship at dock
            // return -1;
        default:
            fprintf(stderr, "Invalid response from solver\n");
            // return -1;
        }
    }
    // //printf("No more guesses available\n");
    return NULL; // No more guesses available
}
// int updateGuess_new(char *guess, int length, int index)
// {
//     // const char validEndChars[] = "56789";
//     // if(index==0)
//     // //printf("guess is : %s  %d\n", guess,index);
//     // const char validChars[] = "56789.";
//     char validEndChars[10];
//     char validChars[10];
//     // // //printf("index : %d\n", index);
//     // // if(index==0)
//     // // {
//     // //     //printf("valid chars : %s\n", valid_chars_perm[index]);
//     // // //printf("valid end chars : %s\n", valid_end_chars_perm[index]);
//     // // }

//     strcpy(validEndChars, valid_chars_perm[index]);
//     strcpy(validChars, valid_end_chars_perm[index]);
//     // if(index==0)
//     // {
//     // //printf("valid chars : %s\n", validChars);
//     // //printf("valid end chars : %s\n", validEndChars);
//     // }
//     int endCharsLen = strlen(validEndChars);
//     int charsLen = strlen(validChars);
//     // // Start from the right (least significant digit) and increment
//     // for (int i = length - 2; i >= 1; i--)
//     // {
//     //     // Find current character's position in valid chars
//     //     char *pos = strchr(validChars, guess[i]);
//     //     int charIndex = pos - validChars;

//     //     if (charIndex < charsLen - 1)
//     //     {
//     //         // Can increment this position
//     //         guess[i] = validChars[charIndex + 1];
//     //         return 1; // Successfully generated next guess
//     //     }
//     //     else
//     //     {
//     //         // Reset this position and continue to the next digit
//     //         guess[i] = validChars[0];
//     //     }
//     // }

//     // // Handle the first and last characters (cannot be '.')
//     // // First, try to increment the last character
//     // char *lastPos = strchr(validEndChars, guess[length - 1]);
//     // int lastIndex = lastPos - validEndChars;

//     // if (lastIndex < endCharsLen - 1)
//     // {
//     //     guess[length - 1] = validEndChars[lastIndex + 1];
//     //     return 1;
//     // }
//     // else
//     // {
//     //     // Reset last character and try first character
//     //     guess[length - 1] = validEndChars[0];

//     //     char *firstPos = strchr(validEndChars, guess[0]);
//     //     int firstIndex = firstPos - validEndChars;

//     //     if (firstIndex < endCharsLen - 1)
//     //     {
//     //         //printf("last char of End char is : %c\n", firstIndex+1);
//     //         guess[0] = validEndChars[firstIndex + 1];
//     //         return 1;
//     //     }
//     // }
//     // const char validEndChars[] = "56789";
//     // const char validChars[] = "56789.";
//     // int endCharsLen = strlen(validEndChars);
//     // int charsLen = strlen(validChars);
//     // if (index == 0)
//     //     //printf("guess is : %s  %d\n", guess, index);
//     // Start from the right (least significant digit) and increment
//     for (int i = length - 2; i >= 1; i--)
//     {
//         // Find current character's position in valid chars
//         char *pos = strchr(validChars, guess[i]);
//         int charIndex = pos - validChars;

//         if (charIndex < charsLen - 1)
//         {
//             // Can increment this position
//             guess[i] = validChars[charIndex + 1];
//             return 1; // Successfully generated next guess
//         }
//         else
//         {
//             // Reset this position and continue to the next digit
//             guess[i] = validChars[0];
//         }
//     }

//     // Handle the first and last characters (cannot be '.')
//     // First, try to increment the last character
//     char *lastPos = strchr(validEndChars, guess[length - 1]);
//     int lastIndex = lastPos - validEndChars;

//     if (lastIndex < endCharsLen - 1)
//     {
//         guess[length - 1] = validEndChars[lastIndex + 1];
//         return 1;
//     }
//     else
//     {
//         // Reset last character and try first character
//         guess[length - 1] = validEndChars[0];

//         char *firstPos = strchr(validEndChars, guess[0]);
//         int firstIndex = firstPos - validEndChars;

//         if (firstIndex < endCharsLen - 1)
//         {
//             guess[0] = validEndChars[firstIndex + 1];
//             return 1;
//         }
//     }
//     // if (index == 0)
//     //     //printf(" new guess is : %s  %d\n", guess, index);
//     // If we've reached here, we've tried all combinations
//     return 0; // No more guesses available
// }
// int undock(int length, ShipRequest ship, int n_solvers, int solver_keys[n_solvers])
// {
//     // Create message queues for each solver
//     int dockId = (-ship.waitingTime) - 1;
//     int msg_id_solver[n_solvers];
//     for (int i = 0; i < n_solvers; i++)
//     {
//         msg_id_solver[i] = msgget(solver_keys[i], PERMS | IPC_CREAT);
//         if (msg_id_solver[i] == -1)
//         {
//             perror("Error creating solver message queue");
//             return -1;
//         }
//     }

//     // Step 1: Inform solvers about which dock we're guessing for
//     for (int i = 0; i < n_solvers; i++)
//     {
//         SolverRequest request;
//         memset(&request, 0, sizeof(request));
//         request.mtype = 1; // Initial communication message type
//         request.dockId = dockId;
//         // authStringGuess field should be ignored in this message

//         if (msgsnd(msg_id_solver[i], &request, sizeof(request) - sizeof(long), 0) == -1)
//         {
//             perror("Error sending initial message to solver");
//             return -1;
//         }
//     }

//     // Step 2: Start guessing frequency string
//     char guessString[length];
//     int guessCorrect = 0;

//     // Generate initial guess based on string properties:
//     // - Characters can be 5, 6, 7, 8, 9, or '.'
//     // - First and last characters cannot be '.'
//     // - Length is specified by the input parameter

//     generateInitialGuess(guessString, length);

//     while (!guessCorrect)
//     {
//         // Send guess to a solver
//         int chosenSolver = rand() % n_solvers; // Choose a solver randomly

//         SolverRequest request;
//         memset(&request, 0, sizeof(request));
//         request.mtype = 2; // Guess message type
//         request.dockId = dockId;
//         strcpy(request.authStringGuess, guessString);

//         if (msgsnd(msg_id_solver[chosenSolver], &request, sizeof(request) - sizeof(long), 0) == -1)
//         {
//             perror("Error sending guess to solver");
//             return -1;
//         }

//         // Wait for response
//         SolverResponse response;
//         if (msgrcv(msg_id_solver[chosenSolver], &response, sizeof(response) - sizeof(long), 3, 0) == -1)
//         {
//             perror("Error receiving response from solver");
//             return -1;
//         }

//         // Process response
//         switch (response.guessIsCorrect)
//         {
//         case 0:
//             // Guess is incorrect, try a new guess
//             updateGuess(guessString, length);
//             //printf("wrong pass %s", guessString);
//             break;
//         case 1:
//             // Guess is correct!
//             guessCorrect = 1;
//             //printf("\ncorrect pass ! %s \n", guessString);
//             break;
//         case -1:
//             // Either dock not set, cargo not moved, or no ship at dock
//             return -1;
//         default:
//             fprintf(stderr, "Invalid response from solver\n");
//             return -1;
//         }
//     }

//     // Step 3: Put the correct string in authStrings field of shared memory
//     updateSharedMemory(dockId, guessString);

//     // Step 4: Send message to validation
//     // int validation_queue = msgget(, PERMS);
//     // if (validation_queue == -1) {
//     //     perror("Error accessing validation message queue");
//     //     return -1;
//     // }

//     MessageStruct validationMsg;
//     memset(&validationMsg, 0, sizeof(validationMsg));
//     validationMsg.mtype = 3;
//     validationMsg.dockId = dockId;
//     validationMsg.shipId = ship.shipId;
//     validationMsg.direction = ship.direction;

//     if (msgsnd(mmqid, &validationMsg, sizeof(validationMsg) - sizeof(long), 0) == -1)
//     {
//         perror("Error sending message to validation");
//         return -1;
//     }
//     else
//     {
//         //printf("notified validation about undocking");
//     }

//     return 0;
// }

// Helper functions that would need to be implemented
// void generateInitialGuess(char *guess, int length)
// {
//     const char validEndChars[] = "56789";
//     const char validChars[] = "56789.";

//     // Set all characters to the first valid value
//     guess[0] = validEndChars[0]; // First char can't be '.'
//     for (int i = 1; i < length - 1; i++)
//     {
//         guess[i] = validChars[0]; // Middle characters can be any valid char
//     }
//     guess[length - 1] = validEndChars[0]; // Last char can't be '.'
//     guess[length] = '\0';
// }

// Move to the next guess systematically
// int updateGuess(char *guess, int length)
// {
//     const char validEndChars[] = "56789";
//     const char validChars[] = "56789.";
//     int endCharsLen = strlen(validEndChars);
//     int charsLen = strlen(validChars);

//     // Start from the right (least significant digit) and increment
//     for (int i = length - 2; i >= 1; i--)
//     {
//         // Find current character's position in valid chars
//         char *pos = strchr(validChars, guess[i]);
//         int charIndex = pos - validChars;

//         if (charIndex < charsLen - 1)
//         {
//             // Can increment this position
//             guess[i] = validChars[charIndex + 1];
//             return 1; // Successfully generated next guess
//         }
//         else
//         {
//             // Reset this position and continue to the next digit
//             guess[i] = validChars[0];
//         }
//     }

//     // Handle the first and last characters (cannot be '.')
//     // First, try to increment the last character
//     char *lastPos = strchr(validEndChars, guess[length - 1]);
//     int lastIndex = lastPos - validEndChars;

//     if (lastIndex < endCharsLen - 1)
//     {
//         guess[length - 1] = validEndChars[lastIndex + 1];
//         return 1;
//     }
//     else
//     {
//         // Reset last character and try first character
//         guess[length - 1] = validEndChars[0];

//         char *firstPos = strchr(validEndChars, guess[0]);
//         int firstIndex = firstPos - validEndChars;

//         if (firstIndex < endCharsLen - 1)
//         {
//             guess[0] = validEndChars[firstIndex + 1];
//             return 1;
//         }
//     }

//     // If we've reached here, we've tried all combinations
//     return 0; // No more guesses available
// }

void updateSharedMemory(int dockId, const char *authString)
{
    // Get shared memory segment
    //printf(" last ship index is %d \n", lastship_index);
    //printf(" updating shared mem with %s \n", authString);
    if (shmid == -1)
    {
        perror("shmget failed in updateSharedMemory");
        return;
    }

    // Attach to shared memory segment
    MainSharedMemory *shm = (MainSharedMemory *)shmat(shmid, NULL, 0);
    if (shm == (MainSharedMemory *)-1)
    {
        perror("shmat failed in updateSharedMemory");
        return;
    }

    // Update the auth string for the specified dock
    strcpy(shm->authStrings[dockId], authString);
    shm->authStrings[dockId][MAX_AUTH_STRING_LEN - 1] = '\0'; // Ensure null termination

    // Detach from shared memory
    if (shmdt(shm) == -1)
    {
        perror("shmdt failed in updateSharedMemory");
    }

    //printf("Updated shared memory for dock %d with auth string: %s\n", dockId, authString);
    freedockwithid(dockId);
}
void increaseTimestamp(int mmqid)
{
    MessageStruct timestamp_msg;
    timestamp_msg.mtype = 5;
    if (msgsnd(mmqid, &timestamp_msg, sizeof(MessageStruct) - sizeof(long), 0) == -1)
    {
        perror("err in increasing timestamps");
    }
    else
    {
        //printf("Ending Timestamp %d\n \n", curr_timestamp);
        curr_timestamp++;
    }
}
void merge(int cargo[][2], int left, int mid, int right)
{
    int i, j, k;
    int n1 = mid - left + 1;
    int n2 = right - mid;

    // Create temporary arrays
    int L[n1][2], R[n2][2];

    // Copy data to temporary arrays L[] and R[]
    for (i = 0; i < n1; i++)
    {
        L[i][0] = cargo[left + i][0]; // Copy cargo values
        L[i][1] = cargo[left + i][1]; // Copy original indices
    }
    for (j = 0; j < n2; j++)
    {
        R[j][0] = cargo[mid + 1 + j][0]; // Copy cargo values
        R[j][1] = cargo[mid + 1 + j][1]; // Copy original indices
    }

    // Merge the temporary arrays back into cargo[left..right]
    i = 0;    // Initial index of first subarray
    j = 0;    // Initial index of second subarray
    k = left; // Initial index of merged subarray

    while (i < n1 && j < n2)
    {
        if (L[i][0] <= R[j][0])
        {
            cargo[k][0] = L[i][0];
            cargo[k][1] = L[i][1];
            i++;
        }
        else
        {
            cargo[k][0] = R[j][0];
            cargo[k][1] = R[j][1];
            j++;
        }
        k++;
    }

    // Copy the remaining elements of L[], if any
    while (i < n1)
    {
        cargo[k][0] = L[i][0];
        cargo[k][1] = L[i][1];
        i++;
        k++;
    }

    // Copy the remaining elements of R[], if any
    while (j < n2)
    {
        cargo[k][0] = R[j][0];
        cargo[k][1] = R[j][1];
        j++;
        k++;
    }
}

/**
 * Recursive function to implement merge sort on cargo[][2]
 *
 * @param cargo The 2D array with cargo values and original indices
 * @param left Starting index
 * @param right Ending index
 */
void merge_sort(int cargo[][2], int left, int right)
{
    if (left < right)
    {
        // Find the middle point
        int mid = left + (right - left) / 2;

        // Sort first and second halves
        merge_sort(cargo, left, mid);
        merge_sort(cargo, mid + 1, right);

        // Merge the sorted halves
        merge(cargo, left, mid, right);
    }
}
int binary_search(const int arr[][2], int start, int end, int target)
{
    int left = start;
    int right = end;
    int result = -1;

    while (left <= right)
    {
        // Calculate middle index (avoiding overflow)
        int mid = left + (right - left) / 2;

        // Check if target is present at mid
        if (arr[mid][0] <= target)
        {
            if (arr[mid][0] != 0)
                result = mid;
            left = mid + 1;
        }
        // If target is greater, ignore left half
        // If target is smaller, ignore right half
        else
        {
            right = mid - 1;
        }
    }

    // Target was not found or rightmost element <= target was found
    return result;
}
void freedockwithid(int dockid)
{
    for (int i = 0; i < n_docks; i++)
    {
        if (dock_status[i][0] == dockid)
        {
            dock_status[i][2] = -1;
            // free the dock
        }
    }
}
void *thrsolver(void *arg)
{
    ThreadData *data = (ThreadData *)arg;
    char *result = undock_new(data->length, data->solver_id, data->ship, data->index, data->capacity, data->start_string);
    //printf("Thread %d: Result: %s Ship %d\n", data->index, result, data->ship.shipId);
    if (result != NULL)
    {
        // Cancel all other threads
        // for (int i = 0; i < n_solvers; i++)
        // {
        //     if (i != data->index)
        //     {
        //         pthread_cancel(threads[i]);
        //     }
        // }
        pthread_exit((void *)result); // Return the result string
    }
    pthread_exit(NULL);
}
int charToValue(char c)
{
    if (c >= '5' && c <= '9')
        return c - '5';
    else if (c == '.')
        return 5;
    return -1; // Invalid
}
int powe(int a, int b)
{
    int result = 1;
    for (int i = 0; i < b; i++)
        result *= a;
    return result;
}
// Function to convert a character value back to character
char valueToChar(int value, int isEdgePosition)
{
    if (value >= 0 && value <= 4)
    {
        return '5' + value; // Maps 0-4 to '5'-'9'
    }
    else if (value == 5 && !isEdgePosition)
    {
        return '.'; // Maps 5 to '.' only for non-edge positions
    }
    return '?'; // Invalid value
}

// Convert a string to its numeric representation
long long int stringToValue(const char *str)
{
    int length = strlen(str);
    long long int value = 0;

    for (int i = 0; i < length; i++)
    {
        int charVal = charToValue(str[i]);
        if (charVal == -1)
        {
            //printf("Invalid character in string: %c\n", str[i]);
            return -1;
        }

        int base;
        if (i == 0 || i == length - 1)
            base = 5; // Edge positions only allow 5-9
        else
            base = 6; // Middle positions allow 5-9 and .

        value = value * base + charVal;
    }

    return value;
}

// Convert a numeric value back to its string representation
char *valueToString(long long int value, int length)
{
    char *result = (char *)malloc((length + 1) * sizeof(char));
    if (!result)
    {
        //printf("Memory allocation failed\n");
        return NULL;
    }
    result[length] = '\0';

    // Convert the numeric value to string
    for (int i = length - 1; i >= 0; i--)
    {
        int base = (i == 0 || i == length - 1) ? 5 : 6;
        int remainder = value % base;
        value /= base;

        result[i] = valueToChar(remainder, (i == 0 || i == length - 1));
    }

    return result;
}

// Calculate the total number of possible strings
long long int calculateTotalStrings(int length)
{
    if (length < 2)
        return 5;

    // First and last positions: 5 possibilities each (5-9)
    // Middle positions: 6 possibilities each (5-9 and .)
    return 5 * powe(6, length - 2) * 5;
}

// Function to partition strings and return start strings and capacities
void partitionStringsWithArrays(int length, int numPartitions, char ***startStrings, int **capacities)
{
    long long int totalStrings = calculateTotalStrings(length);
    long long int basePartitionSize = totalStrings / numPartitions;
    long long int remainder = totalStrings % numPartitions;

    // Allocate arrays
    *startStrings = (char **)malloc(numPartitions * sizeof(char *));
    *capacities = (int *)malloc(numPartitions * sizeof(long long int));

    if (!(*startStrings) || !(*capacities))
    {
        //printf("Memory allocation failed\n");
        return;
    }

    long long int currentValue = 0;

    for (int i = 0; i < numPartitions; i++)
    {
        // Calculate this partition's size (distribute remainder evenly)
        long long int partitionSize = basePartitionSize;
        if (i < remainder)
        {
            partitionSize++;
        }

        // Store the capacity for this partition
        (*capacities)[i] = partitionSize;

        // Generate and store the start string for this partition
        (*startStrings)[i] = valueToString(currentValue, length);

        // Move to the start of the next partition
        currentValue += partitionSize;
    }
}
char *getnext(const char *guessString, int length)
{
    // Allocate memory for the new string
    char *nextString = (char *)malloc((length + 1) * sizeof(char));
    if (!nextString)
    {
        //printf("Memory allocation failed\n");
        return NULL;
    }

    // Copy the original string
    strcpy(nextString, guessString);
    nextString[length] = '\0';

    // Start from the rightmost position
    int i = length - 1;
    while (i >= 0)
    {
        // If current character is 5, increment to 6
        if (nextString[i] == '5')
        {
            nextString[i] = '6';
            break;
        }
        // If current character is 6, increment to 7
        else if (nextString[i] == '6')
        {
            nextString[i] = '7';
            break;
        }
        // If current character is 7, increment to 8
        else if (nextString[i] == '7')
        {
            nextString[i] = '8';
            break;
        }
        // If current character is 8, increment to 9
        else if (nextString[i] == '8')
        {
            nextString[i] = '9';
            break;
        }
        // If current character is 9 and not at edge position, increment to .
        else if (nextString[i] == '9' && i > 0 && i < length - 1)
        {
            nextString[i] = '.';
            break;
        }
        // If current character is . or 9 at edge position, reset to 5 and move left
        else
        {
            nextString[i] = '5';
            i--;
        }
    }

    // If we couldn't increment any digit (reached the maximum)
    if (i < 0)
    {
        //printf("Reached the maximum possible string\n");
        // Return the original string or handle as needed
        free(nextString);
        return NULL;
    }

    return nextString;
}
#include <stdio.h>
#include <stdbool.h>

bool CheckPossibility(int c, int n, int C[], int W[], int T)
{
    int cargo_idx = n - 1;
    for(int i = c - 1; i >= 0 && cargo_idx >= 0; i--)
    {
        int capacity = C[i];
        int items_to_handle = T;
        while(items_to_handle > 0 && cargo_idx >= 0)
        {
            if(W[cargo_idx] <= capacity)
            {
                cargo_idx--;
                items_to_handle--;
            }
            else
            {
                break;
            }
        }
    }
    
    return cargo_idx < 0;
}

int MinimumTime(int no_cranes, int no_cargo, int sorted_cranecapacity[], int sorted_shipcargo[])
{
    int left = 1;
    int right = (no_cargo + no_cranes - 1) / no_cranes;
    int result = -1;
    while (left <= right)
    {
        int mid = (left + right) / 2;
        if(CheckPossibility(no_cranes, no_cargo, sorted_cranecapacity, sorted_shipcargo, mid))
        {
            result = mid;
            right = mid - 1;
        }
        else
        {
            left = mid + 1;
        }
    }
    return result;
}

