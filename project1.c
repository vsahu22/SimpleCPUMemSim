#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <time.h>

int memoryAccessError(int PC, int address, int parentToChild[]); // Prototyping function used to exit if user program is trying to access system memory

int main(int argc, char* argv[]) {

    pid_t memorypid;
    int timerIntValue = atoi(argv[2]); // Value when the timer will interrupt 
    int parentToChild[2]; // fd for pipe for sending data from CPU to Memory
    int childToParent[2]; // fd for pipe for sending data from Memory to CPU
    if (pipe(parentToChild) == -1) { // Creating the pipe that will send CPU data to Memory
        perror("Error with creating pipe");
        exit(EXIT_FAILURE);
    }
    if (pipe(childToParent) == -1) { // Creating the pipe that will send Memory data to CPU
        perror("Error with creating pipe");
        exit(EXIT_FAILURE);
    }
    memorypid = fork(); // Forking program into two separate processes: CPU and Memory
    if (memorypid < 0) {
        perror("Error while forking");
        exit(EXIT_FAILURE);
    }
    if (memorypid == 0) { // Memory process is the child process
        int memory[2000]; // Memory array, array of ints
        char buf[150]; // Used for loader. Line read from file is stored here
        int address = 0;
        int CPUData;
        int i;  // Just used in for loop, cant initialize within it 
        FILE *inputFile = fopen(argv[1], "r");
        if (inputFile == NULL) { // Checking if file has opened correctly
            perror("Error while opening input file");
            exit(EXIT_FAILURE);
        }
        while (fgets(buf, sizeof(buf), inputFile)) { // Loading integers into memory, parsing in order to bypass comments
            if (buf[0] == ' ' || buf[0] == '\n') { // If the line is blank, the loader skips it
                continue;
            }
            for (i = 0; i < sizeof(buf); i++) {
                if (buf[i] == ' ' || buf[i] == '\n') { // Integer followed by newline or whitespace, so we terminate right after it
                    buf[i] = '\0';
                    break;
                }
            }
            if (buf[0] == '.') { // If loader finds a '.', it jumps to address after dot and starts loading from there
                address = atoi(&buf[1]); // The atoi argument ptr starts at the second element so the '.' is skipped over
                continue;
            }
            memory[address] = atoi(buf); // Converting string to int and storing into memory
        
            address++;
        }

        close(parentToChild[1]); // Closing write end since we only read from this pipe
        close(childToParent[0]); // Closing read end since we only write from this pipe to parent

        while (1) {
            
            read(parentToChild[0], &CPUData, sizeof(CPUData)); // Reading data sent from the CPU
            if (CPUData >= 10000) { // This means that the memory needs to write to the given address
                address = CPUData % 10000;  // Extracting address from CPU data
                read(parentToChild[0], &CPUData, sizeof(CPUData)); // Getting value from CPU that needs to be written to memory
                memory[address] = CPUData; // Writing value to memory
            }
            else if (CPUData == -1) { // CPU sending memory a signal to terminate
                _exit(0);
            }
            else { // If memory needs to read from the given address
                write(childToParent[1], &memory[CPUData], sizeof(int)); // Writing value at address back to CPU
            }

        }
        
    }
    else { // Parent process which is the CPU
        int PC, IR, AC, X, Y = 0; // Initializing system registers 
        int SP = 1000; // Stack pointer initialized at 1000 since it resides at end of user program memory (one address higher)
        int operand = 0; // Local variable used to store operand
        int timerCount = 0; // Timer that keeps track of the number of instructions executed by the CPU
        close(parentToChild[0]); // Closing read end since we want to write to memory
        close(childToParent[1]); // Closing write end since we want to read from this pipe
        while (IR != 50) { // Infinite loop that ends when the end instruction is executed

            
            write(parentToChild[1], &PC, sizeof(PC)); // Writing the program counter to the memory
            PC++; // PC is incremented after it is written
            read(childToParent[0], &IR, sizeof(IR)); // Reading the address from memory into IR
            
            switch(IR) {
                case 1: // Load value
                    write(parentToChild[1], &PC, sizeof(PC)); // Send PC to memory
                    PC++;
                    read(childToParent[0], &AC, sizeof(AC)); // Loading value at PC address into AC
                    //printf("Loading %d\n", AC);
                    break;
                
                case 2: // Loading value at address into AC
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Getting address from the memory and storing in operand
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    } 
                    write(parentToChild[1], &operand, sizeof(operand)); // Sending the address to memory in order to get the value
                    read(childToParent[0], &AC, sizeof(AC)); // Loading value at the address into the AC
                    break;

                case 3: // LoadInd - Load the value from the address found in the given address into the AC
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Address is stored into operand
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    write(parentToChild[1], &operand, sizeof(operand)); // Address is sent back to memory in order to get value (new address)
                    read(childToParent[0], &operand, sizeof(operand)); // Reading value in order to get new address
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    write(parentToChild[1], &operand, sizeof(operand)); // Writing the new address back in order to get value
                    read(childToParent[0], &AC, sizeof(AC)); // Loading the value at the new address into the AC
                    break;

                case 4: // LoadIdxX addr - Load the value at (address+X) into the AC
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Address is stored into operand
                    operand += X; // Adding X to the address in order to get new address
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    write(parentToChild[1], &operand, sizeof(operand)); // Sending new address to memory in order to get value
                    read(childToParent[0], &AC, sizeof(AC)); //  Loading value at new address into AC
                    break;
                
                case 5: // LoadIdxY addr - Load the value at (address+Y) into the AC
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Address is stored into operand
                    operand += Y; // Adding Y to the address in order to get new address
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    write(parentToChild[1], &operand, sizeof(operand)); // Sending new address to memory in order to get value
                    read(childToParent[0], &AC, sizeof(AC)); //  Loading value at new address into AC
                    break;

                case 6: // LoadSpX - Load from (Sp+X) into the AC
                    operand = SP + X; // Adding SP and X to get address to load from
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    write(parentToChild[1], &operand, sizeof(operand)); // Sending address to memory in order to get value
                    read(childToParent[0], &AC, sizeof(AC)); // Loading value at address into AC
                    break;
                
                case 7: // Store addr - Store the value in the AC into the address
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Reading address from memory that we want to store the AC in
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    operand += 10000; // Increasing address by 1 digit place than total memory size so we can use % to get address
                    // Using 10000 since it is one digit larger than 2000
                    write(parentToChild[1], &operand, sizeof(operand)); // Writing it to memory
                    write(parentToChild[1], &AC, sizeof(AC)); // Writing the AC value to memory
                    break;

                case 8: // Get - Gets a random int from 1 to 100 into the AC
                    srand(time(0));
                    AC = rand() % 100 + 1;
                    break;

                case 9: // Put port - If port = 1, write AC as int to screen, write AC as char to screen if 2
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Getting port from memory
                    fflush(stdout);
                    if (operand == 1) { // Write AC as int to screen
                        printf("%d", AC);
                    }
                    else if (operand == 2) { // Write AC as char to screen
                        printf("%c", AC);
                    }
                    //printf("Printing\n");
                    break;
                
                case 10: // AddX - Add the value in X to the AC
                    AC += X; 
                    break;

                case 11: // AddY - Add the value in Y to the AC
                    AC += Y;
                    break;

                case 12: // SubX - Subtract the value in X from the AC
                    AC -= X;
                    break;
                
                case 13: // SubY - Subtract the value in Y from the AC
                    AC -= Y;
                    break;

                case 14: // CopyToX - Copy the value in the AC to X
                    X = AC;
                    break;

                case 15: // CopyFromX - Copy the value in X to the AC
                    AC = X;
                    break;
                
                case 16: // CopyToY - Copy the value in the AC to Y
                    Y = AC;
                    break;

                case 17: // CopyFromY - Copy the value in Y to the AC
                    AC = Y;
                    break;

                case 18: // CopyToSP - Copy the value in AC to the SP
                    SP = AC;
                    break;

                case 19: // CopyFromSP - Copy the value in SP to the AC
                    AC = SP;
                    break;

                case 20: // Jump addr - Jump to the address
                    write(parentToChild[1], &PC, sizeof(PC)); // Sending PC to fetch address from memory
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Reading fetched address into operand so we can check if it is valid
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    PC = operand; // Jumping to address 
                    break;

                case 21: // JumpIfEqual addr - Jump to the address only if the value in the AC is zero
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand));
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    if (AC == 0) { // Only if AC is 0, we jump to address
                        PC = operand;
                    }
                    break;

                case 22: // JumpIfNotEqual addr - Jump to the address only if the value in the AC is not zero
                    write(parentToChild[1], &PC, sizeof(PC));
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand));
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    if (AC != 0) { // Only if AC is not 0, we jump to address
                        PC = operand;
                    }
                    break;

                case 23: // Call addr - Push return address onto stack, jump to the address
                    write(parentToChild[1], &PC, sizeof(PC)); // Sending PC to fetch address from memory
                    PC++;
                    read(childToParent[0], &operand, sizeof(operand)); // Reading address into operand
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    SP--; // Stack pointer is decremented since stack grows downwards, the address will be "pushed" to this location
                    SP += 10000; // Temporarily adding 10000 to SP so that the memory knows that it needs to write to its address
                    write(parentToChild[1], &SP, sizeof(SP)); // Writing SP address to memory that the return address will be stored in
                    write(parentToChild[1], &PC, sizeof(PC)); // Writing the return address which is PC
                    SP -= 10000; // Removing 10000 to get original SP address
                    PC = operand; // Jumping to address after it is pushed onto the stack 
                    break;

                case 24: // Ret - Pop return address from the stack, jump to the address
                    write(parentToChild[1], &SP, sizeof(SP)); // Sending the SP in order to pop address from stack
                    read(childToParent[0], &operand, sizeof(operand)); // Retrieving address from stack
                    if (memoryAccessError(PC, operand, parentToChild) == 1) {
                        IR = 50;  // If user program is trying to access system memory, it exits with an error
                        continue;
                    }
                    SP++; // Incrementing the stack pointer since address was popped, so it points to next item
                    PC = operand; // Jumping to address 
                    break;

                case 25: // IncX - Increment the value in X
                    X++;
                    break;

                case 26: // DecX - Decrement the value in X
                    X--;
                    break;
                
                case 27: // Push - Push AC onto the stack
                    SP--; 
                    SP += 10000;
                    write(parentToChild[1], &SP, sizeof(SP)); // New SP address to be written to is sent to memory
                    write(parentToChild[1], &AC, sizeof(AC)); // AC value is being pushed onto stack at SP address
                    SP -= 10000;
                    break;

                case 28: // Pop - Pop from stack into AC
                    write(parentToChild[1], &SP, sizeof(SP)); // Sending the SP in order to pop value from stack
                    read(childToParent[0], &AC, sizeof(AC)); // Reading popped value from stack into AC
                    SP++;
                    break;

                case 29: // Int - Perform system call (Execution at address 1500)
                    if (PC < 1000) { // If the program is in user mode
                        operand = SP; // Operand contains SP which will be saved to system stack
                        SP = 1999; // SP now pointing to system stack
                        SP += 10000;
                        write(parentToChild[1], &SP, sizeof(SP)); // Sending system SP address to memory
                        write(parentToChild[1], &operand, sizeof(operand)); // Pushing old user program stack pointer to system stack
                        SP--;
                        write(parentToChild[1], &SP, sizeof(SP));
                        write(parentToChild[1], &PC, sizeof(PC)); // Pushing user program PC to system stack
                        SP -= 10000;
                        PC = 1500; // Jumping to address 1500 for execution
                    }
                    break;

                case 30: // Iret - Return from system call
                    write(parentToChild[1], &SP, sizeof(SP)); // Sending system SP in order to pop user program PC
                    read(childToParent[0], &PC, sizeof(PC)); // Restoring user program PC from system stack
                    SP++;
                    write(parentToChild[1], &SP, sizeof(SP));
                    read(childToParent[0], &SP, sizeof(SP)); // Restoring user program SP by popping from system stack
                    break;

                case 50: // End - End execution
                    IR = -1; // If -1 is sent to memory, memory knows to terminate
                    write(parentToChild[1], &IR, sizeof(IR));
                    IR = 50;
                    continue;
            }

            timerCount++; // Timer is incremented at the end of each loop, since an instruction is executed
            
            if (timerCount % timerIntValue == 0) { // Timer interrupts every timerIntValue instructions
                    if (PC < 1000) { // If the CPU is in user mode, the interrupt will change it to kernel mode
                        operand = SP; // Operand contains SP which will be saved to system stack
                        SP = 1999; // SP now pointing to system stack
                        SP += 10000;
                        write(parentToChild[1], &SP, sizeof(SP)); // Sending system SP address to memory
                        write(parentToChild[1], &operand, sizeof(operand)); // Pushing old user program stack pointer to system stack
                        SP--;
                        write(parentToChild[1], &SP, sizeof(SP));
                        write(parentToChild[1], &PC, sizeof(PC)); // Pushing user program PC to system stack
                        SP -= 10000;
                        PC = 1000; // Jumping to address 1000 for execution
                    }           
            }

            
        }
    }
    
}

// Function that checks if user program is trying to access system memory or memory that doesn't exist (invalid address)
int memoryAccessError(int PC, int address, int parentToChild[]) { 
                if (PC <= 999) { // If PC is in user program memory 
                    if (address > 999 && address < 2000) { // If the address is in system memory
                        printf("Memory violation: accessing system address %d in user mode\n", address);
                        address = -1;
                        write(parentToChild[1], &address, sizeof(address)); // Sending exit message to memory
                        return 1; // 1 is true - this means that there is an error with memory access 
                    }
                    else if (address < 0 || address >= 2000) { // If the address doesn't exist
                        printf("Error: Invalid memory address %d\n", address);
                        address = -1;
                        write(parentToChild[1], &address, sizeof(address)); // Sending exit message to memory
                        return 1;
                    }
                    else {
                        return 0;
                    }
                }
            }