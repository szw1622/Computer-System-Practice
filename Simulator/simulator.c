/*
 * Author: Daniel Kopta
 * Updated by: Erin Parker
 * CS 4400, University of Utah
 *
 * Simulator handout
 * A simple x86-like processor simulator.
 * Read in a binary file that encodes instructions to execute.
 * Simulate a processor by executing instructions one at a time and appropriately
 * updating register and memory contents.
 *
 * Some code and pseudo code has been provided as a starting point.
 *
 * Completed by: Zhuowen Song
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "instruction.h"

// Forward declarations for helper functions
unsigned int get_file_size(int file_descriptor);
unsigned int *load_file(int file_descriptor, unsigned int size);
instruction_t *decode_instructions(unsigned int *bytes, unsigned int num_instructions);
unsigned int execute_instruction(unsigned int program_counter, instruction_t *instructions,
                                 int *registers, unsigned char *memory);
void print_instructions(instruction_t *instructions, unsigned int num_instructions);
void error_exit(const char *message);

// 17 registers
#define NUM_REGS 17
// 1024-byte stack
#define STACK_SIZE 1024

int main(int argc, char **argv)
{
  // Make sure we have enough arguments
  if (argc < 2)
    error_exit("must provide an argument specifying a binary file to execute");

  // Open the binary file
  int file_descriptor = open(argv[1], O_RDONLY);
  if (file_descriptor == -1)
    error_exit("unable to open input file");

  // Get the size of the file
  unsigned int file_size = get_file_size(file_descriptor);
  // Make sure the file size is a multiple of 4 bytes
  // since machine code instructions are 4 bytes each
  if (file_size % 4 != 0)
    error_exit("invalid input file");

  // Load the file into memory
  // We use an unsigned int array to represent the raw bytes
  // We could use any 4-byte integer type
  unsigned int *instruction_bytes = load_file(file_descriptor, file_size);
  close(file_descriptor);

  unsigned int num_instructions = file_size / 4;

  /****************************************/
  /**** Begin code to modify/implement ****/
  /****************************************/

  // Allocate and decode instructions
  instruction_t *instructions = decode_instructions(instruction_bytes, num_instructions);

  // Optionally print the decoded instructions for debugging
  // Will not work until you implement decode_instructions
  // Do not call this function in your submitted final version
  //print_instructions(instructions, num_instructions);

  // Allocate and initialize registers
  int *registers = (int *)malloc(sizeof(int) * NUM_REGS);
  // Initialize register values
  int i;
  for (i = 0; i < NUM_REGS; i++)
  {
    // All registers should initially have a value of 0
    registers[i] = (int32_t)0;
    
    if (i == 6) // %esp ID is 6
    // %esp is the stack pointer register, and should initially have a value of 1024 
      registers[i] = (int32_t)1024;
  }

  // Stack memory is byte-addressed, so it must be a 1-byte type
  unsigned char *memory = (unsigned char*)malloc(sizeof(char) * STACK_SIZE);
  unsigned int j;
  // Every byte in memory should initially have a value of 0.
  for (j=0; j < STACK_SIZE; j++){
    memory[j] = 0;
  }

  // Run the simulation
  unsigned int program_counter = 0;

  // program_counter is a byte address, so we must multiply num_instructions by 4
  // to get the address past the last instruction
  while (program_counter != num_instructions * 4)
  {
    program_counter = execute_instruction(program_counter, instructions, registers, memory);
  }

  return 0;
}

/*
 * Decodes the array of raw instruction bytes into an array of instruction_t
 * Each raw instruction is encoded as a 4-byte unsigned int
 */
instruction_t *decode_instructions(unsigned int *bytes, unsigned int num_instructions)
{
  // Follow line 72 as reference of malloc()
  // <int* registers = (int*)malloc(sizeof(int) * NUM_REGS);>
  instruction_t* retval = (instruction_t*)malloc(sizeof(instruction_t) * num_instructions);

  int i;
  for (i = 0; i < num_instructions; i++)
  {
    // Set up the mask wich can select 5 bits of the instruction
    unsigned int mask = 0xF8000000;
    unsigned int input = bytes[i];

    retval[i].opcode = (input & mask) >> 27; // Select the most significant byte(opcode)
    mask >>= 5;                              // Next 5 bits
    retval[i].first_register = (input & mask) >> 22;
    mask >>= 5; // Next 5 bits
    retval[i].second_register = (input & mask) >> 17;
    mask = 0xFFFF; // Change the mask to select the 16 least significant bits
    retval[i].immediate = (input & mask);
  }

  return retval;
}

/*
 * Executes a single instruction and returns the next program counter
 */
unsigned int execute_instruction(unsigned int program_counter, instruction_t *instructions, int *registers, unsigned char *memory)
{
  // program_counter is a byte address, but instructions are 4 bytes each
  // divide by 4 to get the index into the instructions array
  instruction_t instr = instructions[program_counter / 4];
  int CF, OF, ZF, SF;

  switch (instr.opcode)
  {
  // 0 reg1 = reg1 - imm
  case subl:
    registers[instr.first_register] = registers[instr.first_register] - instr.immediate;
    break;
  // 1 	reg2 = reg2 + reg1
  case addl_reg_reg:
    registers[instr.second_register] = registers[instr.first_register] + registers[instr.second_register];
    break;

  // 2 reg1 = reg1 + imm
  case addl_imm_reg:
    registers[instr.first_register] = registers[instr.first_register] + instr.immediate;
    break;
  // 3 	reg2 = reg1 * reg2
  case imull:
    registers[instr.second_register] = registers[instr.first_register] * registers[instr.second_register];
    break;
  // 4 reg1 = reg1 >> 1 (logical shift)
  case shrl:
    registers[instr.first_register] = (uint32_t)registers[instr.first_register] >> 1;
    break;
  // 5 reg2 = reg1
  case movl_reg_reg:
    registers[instr.second_register] = registers[instr.first_register];
    break;
  // 6 reg2 = memory[reg1 + imm]   (moves 4 bytes)
  case movl_deref_reg:
    registers[instr.second_register] = *((int*)(&memory[(registers[instr.first_register] + instr.immediate)]));
    break;
  // 7 memory[reg2 + imm] = reg1   (moves 4 bytes)
  case movl_reg_deref:
    *((int*)(&memory[(registers[instr.second_register] + instr.immediate)])) = registers[instr.first_register];
    break;
  // 8 reg1 = sign_extend(imm)
  case movl_imm_reg:
    registers[instr.first_register] = instr.immediate;
    break;

  // 9 perform reg2 - reg1, set condition codes does not modify reg1 or reg2
  case cmpl:
    CF = 0;
    ZF = 0;
    SF = 0;
    OF = 0;
    long reg2_reg1_diff = (long)registers[instr.second_register] - (long)registers[instr.first_register];

    // If reg2 - reg1 results in unsigned overflow, CF is true, bit 0
    if ((unsigned long)registers[instr.first_register] > (unsigned long)registers[instr.second_register])
      CF = 1;
    // If reg2 - reg1 == 0, ZF is true, bit 6
    if (reg2_reg1_diff == 0) 
      ZF = 64;
    // If most significant bit of (reg2 - reg1) is 1, SF is true, bit 7
    if ((reg2_reg1_diff & 0x80000000) != 0)
      SF = 128;
    // If reg2 - reg1 results in signed overflow, OF is true, bit 11
    if (reg2_reg1_diff > INT32_MAX || reg2_reg1_diff < INT32_MIN)
      OF = 2048;
    // Sets the condition codes (CF, ZF, SF, OF) corresponding to certain bits in the %eflags register
    // %eflags ID is 16
    registers[16] = 0;
    registers[16] = CF + OF + SF + ZF;  
    break;
  
  // 10 jump on equal, jump if ZF
  case je:
    if ((registers[16] & 0x40) != 0)
      return program_counter + 4 + (instr.immediate);
    break;
  // 11 jump on less than, jump if SF xor OF
  case jl:
    if (((registers[16] & 0x80) != 0) ^ ((registers[16] & 0x800) != 0))
      return program_counter + 4 + (instr.immediate);
    break;
  // 12 jump on less than or equal, jump if (SF xor OF) or ZF
  case jle:
    if ((((registers[16] & 0x800) != 0) ^ ((registers[16] & 0x80) != 0)) ||  ((registers[16] & 0x40) != 0))
      return program_counter + 4 + (instr.immediate);
    break;
  // 13 jump on greater than or equal, jump if not (SF xor OF)
  case jge:
    if (!( ((registers[16] & 0x800) != 0) ^ ((registers[16] & 0x80) != 0)))
      return program_counter + 4 + (instr.immediate);
    break;
  // 14 jump on below or equal (unsigned), jump if CF or ZF
  case jbe:
    if (((registers[16] & 0x40) != 0) || ((registers[16] & 0x1) != 0))
      return program_counter + 4 + (instr.immediate);
    break;
  // 15 unconditional jump
  case jmp:
    return program_counter + 4 + (instr.immediate);
    break;
  // 16 %esp = %esp - 4, memory[%esp] = program_counter + 4, jump to target
  // %esp ID is 6
  case call:
    registers[6] = registers[6] - 4;
    *((int*)&memory[registers[6]]) = program_counter + 4;
    return program_counter + instr.immediate + 4;
    break;
  // 17 if %esp == 1024, exit simulation
  // else program_counter = memory[%esp]
  // %esp = %esp + 4
  case ret:
    if(registers[6] == 1024)
      exit(0);
    else
      program_counter = *((int*)(&memory[registers[6]]));
      registers[6] = registers[6] + 4;
      return program_counter;
    break;
  // 18 %esp = %esp - 4, memory[%esp] = reg1
  case pushl:
    registers[6] = registers[6] - 4;
    *((int*)&memory[registers[6]]) = registers[instr.first_register];
    break;
  // 19 reg1 = memory[%esp], %esp = %esp + 4
  case popl:
    registers[instr.first_register] = *((int*)(&memory[registers[6]]));
    registers[6] = registers[6] + 4;
    break;
  // 20 print the value of reg1
  case printr:
    printf("%d (0x%x)\n", registers[instr.first_register], registers[instr.first_register]);
    break;
  // 21 user input an integer to reg1
  case readr:
    scanf("%d", &(registers[instr.first_register]));
    break;

  }

  // program_counter + 4 represents the subsequent instruction
  return program_counter + 4;
}

/*********************************************/
/****  DO NOT MODIFY THE FUNCTIONS BELOW  ****/
/*********************************************/

/*
 * Returns the file size in bytes of the file referred to by the given descriptor
 */
unsigned int get_file_size(int file_descriptor)
{
  struct stat file_stat;
  fstat(file_descriptor, &file_stat);
  return file_stat.st_size;
}

/*
 * Loads the raw bytes of a file into an array of 4-byte units
 */
unsigned int *load_file(int file_descriptor, unsigned int size)
{
  unsigned int *raw_instruction_bytes = (unsigned int *)malloc(size);
  if (raw_instruction_bytes == NULL)
    error_exit("unable to allocate memory for instruction bytes (something went really wrong)");

  int num_read = read(file_descriptor, raw_instruction_bytes, size);

  if (num_read != size)
    error_exit("unable to read file (something went really wrong)");

  return raw_instruction_bytes;
}

/*
 * Prints the opcode, register IDs, and immediate of every instruction,
 * assuming they have been decoded into the instructions array
 */
void print_instructions(instruction_t *instructions, unsigned int num_instructions)
{
  printf("instructions: \n");
  unsigned int i;
  for (i = 0; i < num_instructions; i++)
  {
    printf("op: %d, reg1: %d, reg2: %d, imm: %d\n",
           instructions[i].opcode,
           instructions[i].first_register,
           instructions[i].second_register,
           instructions[i].immediate);
  }
  printf("--------------\n");
}

/*
 * Prints an error and then exits the program with status 1
 */
void error_exit(const char *message)
{
  printf("Error: %s\n", message);
  exit(1);
}
