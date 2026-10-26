#ifndef NJEMU_TEST_PS2_KERNEL_H
#define NJEMU_TEST_PS2_KERNEL_H

typedef struct {
	int init_count;
	int max_count;
	int option;
} ee_sema_t;

int CreateSema(ee_sema_t *sema);
int DeleteSema(int id);
int SignalSema(int id);
int WaitSema(int id);

#endif
