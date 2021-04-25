#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/ipc.h>

#define ERROR -1
#define SUCCESS 0
#define FALSE 0
#define TRUE 1

#define MEMORY_SIZE 256
#define NUM_OF_PROCESSES 3
#define FIFO_NAME "FIFO"
#define PIDS_FILE_NAME "PIDs"

unsigned char isPaused = FALSE;
unsigned char isParentProcess = TRUE;

pid_t pid[NUM_OF_PROCESSES];	//Processes IDs
int idSem1_2; 					//Semaphore ID between Producer and first Consumer
int idSem2_3; 					//Semaphore ID between first Consumer and second Consumer
int idSharedMem1_2;  			//Shared memory ID between Producer and first Consumer
int idSharedMem2_3;  			//Shared memory ID between first Consumer and second Consumer
char *sharedMem1_2;  			//Pointer to block of shared memory
int *sharedMem2_3;   			//Pointer to block of shared memory
int fileDescriptor;				//FIFO file descriptor

void process1(void);			//Producer
void process2(void);			//First consumer
void process3(void);			//Second consumer

unsigned char init_resources(void);
unsigned char cleanup_resources(void);

void init_signals(void);
void handle_signal(int sig);
void handle_s4(int sig);

void sem_signal(int semNumber, int id);
void sem_wait(int semNumber, int id);

int main(int argc, char *argv[])
{	
	if (init_resources() == SUCCESS)
		printf("\nSuccessfuly initialized resources!\n\n");
	else
		exit(EXIT_FAILURE);
		
	init_signals();
	
	if ((pid[0] = fork()) == 0)
	{
		isParentProcess = FALSE;
		init_signals();
		process1();
	}
	
	if ((pid[1] = fork()) == 0)
	{
		isParentProcess = FALSE;
		init_signals();
		process2();
	}

	if ((pid[2] = fork()) == 0)
	{
		isParentProcess = FALSE;
		init_signals();
		process3();
	}
	
	FILE *pidsFile = fopen(PIDS_FILE_NAME, "w");
	if (pidsFile == NULL)
	{
		cleanup_resources();
		fprintf(stderr, "Couldn't open PIDs file!\n");
		exit(EXIT_FAILURE);
	}
	
	fprintf(pidsFile, "%d %d %d", pid[0], pid[1], pid[2]);
	fclose(pidsFile);
	
	waitpid(pid[0], NULL, 0);
	waitpid(pid[1], NULL, 0);
	waitpid(pid[2], NULL, 0);
	
	if (cleanup_resources() == SUCCESS)
		printf("Successfuly cleaned up resources!\n\n");
		
	execlp("ipcs", "ipcs", NULL);
	
	return 0;
}

void process1(void)
{
	semctl(idSem1_2, 0, SETVAL, 1);
    semctl(idSem1_2, 1, SETVAL, 0);
    
    FILE *pidsFile = fopen(PIDS_FILE_NAME, "r");
	while (pidsFile == NULL)
		pidsFile = fopen(PIDS_FILE_NAME, "r");
	
	while (pid[0] == 0 || pid[1] == 0 || pid[2] == 0)
		fscanf(pidsFile, "%d %d %d", &pid[0], &pid[1], &pid[2]);
		
	fclose(pidsFile);
    
    printf("Enter text: \n");
    
	char line[MEMORY_SIZE];
	while (TRUE)
	{	
		while (isPaused == TRUE);
		
		sem_wait(0, idSem1_2);

		if (fgets(line, MEMORY_SIZE, stdin) != NULL)
			line[strlen(line) - 1] = '\0';
		
		strncpy(sharedMem1_2, line, strlen(line) + 1);
		
		sem_signal(1, idSem1_2);
	}
}

void process2(void)
{
	semctl(idSem2_3, 0, SETVAL, 1);
    semctl(idSem2_3, 1, SETVAL, 0);
    
    FILE *pidsFile = fopen(PIDS_FILE_NAME, "r");
	while (pidsFile == NULL)
		pidsFile = fopen(PIDS_FILE_NAME, "r");
	
	while (pid[0] == 0 || pid[1] == 0 || pid[2] == 0)
		fscanf(pidsFile, "%d %d %d", &pid[0], &pid[1], &pid[2]);
		
	fclose(pidsFile);
    
	int textLength = 0;
	
	while (TRUE)
	{
		while (isPaused == TRUE);
		
		sem_wait(1, idSem1_2);
		
		if (strlen(sharedMem1_2) == 0)
			textLength = -1;
		else
			textLength = strlen(sharedMem1_2);
			
		sem_signal(0, idSem1_2);
		
		sem_wait(0, idSem2_3);
		
		if (isPaused == TRUE)
			*sharedMem2_3 = -1;
		else
			*sharedMem2_3 = textLength;
			
		sem_signal(1, idSem2_3);
	}
}

void process3(void)
{
	FILE *pidsFile = fopen(PIDS_FILE_NAME, "r");
	while (pidsFile == NULL)
		pidsFile = fopen(PIDS_FILE_NAME, "r");
	
	while (pid[0] == 0 || pid[1] == 0 || pid[2] == 0)
		fscanf(pidsFile, "%d %d %d", &pid[0], &pid[1], &pid[2]);
		
	fclose(pidsFile);
	
	while (TRUE)
	{
		while (isPaused == TRUE);
		
		sem_wait(1, idSem2_3);
		
		if (*sharedMem2_3 > 0 && isPaused == FALSE)
		{
			printf("Text length = %d\n\n", *sharedMem2_3);
			*sharedMem2_3 = -1;
		}
			
		sem_signal(0, idSem2_3);
	}
}

unsigned char init_resources(void)
{
	key_t key = ftok(".", 'r');
	if (key == ERROR)
		return ERROR;
		
	if ((idSem1_2 = semget(key + 1, 2, 0666 | IPC_CREAT)) == ERROR)
		return ERROR;
		
	if ((idSem2_3 = semget(key + 3, 2, 0666 | IPC_CREAT)) == ERROR)
		return ERROR;
		
	if ((idSharedMem1_2 = shmget(key + 2, MEMORY_SIZE * sizeof(char), 0666 | IPC_CREAT)) == ERROR)
		return ERROR;
		
	if ((idSharedMem2_3 = shmget(key + 4, sizeof(int), 0666 | IPC_CREAT)) == ERROR)
		return ERROR;
    
    if ((sharedMem1_2 = (char *)shmat(idSharedMem1_2, NULL, 0)) == (void *)ERROR)
		return ERROR;
		
	if ((sharedMem2_3 = (int *)shmat(idSharedMem2_3, NULL, 0)) == (void *)ERROR)
		return ERROR;
		
	if (mkfifo(FIFO_NAME, 0666) == ERROR)
	{
		if (errno != EEXIST)
			return ERROR;
	}
	
	if ((fileDescriptor = open(FIFO_NAME, O_RDWR)) == ERROR)
		return ERROR;
	
	if (semctl(idSem1_2, 0, SETVAL, 0) == ERROR)
		return ERROR;
		
    if (semctl(idSem1_2, 1, SETVAL, 0) == ERROR)
		return ERROR;
    
	if (semctl(idSem2_3, 0, SETVAL, 0) == ERROR)
		return ERROR;
		
    if (semctl(idSem2_3, 1, SETVAL, 0) == ERROR)
		return ERROR;
		
	return SUCCESS;
}

unsigned char cleanup_resources(void)
{
	if (shmdt(sharedMem1_2) == ERROR)
		return ERROR;
		
	if (shmdt(sharedMem2_3) == ERROR)
		return ERROR;
	
	if (shmctl(idSharedMem1_2, IPC_RMID, NULL) == ERROR)
		return ERROR;
		
	if (shmctl(idSharedMem2_3, IPC_RMID, NULL) == ERROR)
		return ERROR;
	
	if (semctl(idSem1_2, 0, IPC_RMID) == ERROR)
		return ERROR;
		
	if (semctl(idSem2_3, 0, IPC_RMID) == ERROR)
		return ERROR;
	
	if (close(fileDescriptor) == ERROR)
		return ERROR;
		
	if (unlink(FIFO_NAME) == ERROR)
		return ERROR;
	
	if (remove(PIDS_FILE_NAME) == ERROR)
		return ERROR;
		
	return SUCCESS;
}

void init_signals(void)
{	
	sigset_t mask;
	sigfillset(&mask);
		
	if (isParentProcess == TRUE)
		sigprocmask(SIG_SETMASK, &mask, NULL);
	else
	{
		signal(SIGUSR1, handle_signal);
		signal(SIGUSR2, handle_s4);
		signal(SIGURG, handle_signal);
		signal(SIGCONT, handle_signal);

		sigdelset(&mask, SIGUSR2); 
		sigdelset(&mask, SIGCONT);
		sigdelset(&mask, SIGUSR1);
		sigdelset(&mask, SIGURG);
		sigprocmask(SIG_SETMASK, &mask, NULL);
	}
}

void handle_signal(int sig)
{
	int i;
	for (i = 0; i < NUM_OF_PROCESSES; i++)
	{
		if (write(fileDescriptor, &sig, sizeof(sig)) == ERROR)
			fprintf(stderr, "Couldn't write message to FIFO!\n");
	}

	if (sig == SIGURG)
	{
		printf("Paused!\n");
		kill(pid[2], SIGUSR2);
		kill(pid[1], SIGUSR2);
		kill(pid[0], SIGUSR2);
	}
	else if (sig == SIGCONT)
	{
		printf("Unpaused!\n\n");
		kill(pid[0], SIGUSR2);
		kill(pid[1], SIGUSR2);
		kill(pid[2], SIGUSR2);
	}
	else if (sig == SIGUSR1)
	{
		if (getpid() == pid[0])
		{
			kill(pid[1], SIGUSR2);
			kill(pid[2], SIGUSR2);
			kill(pid[0], SIGUSR2);
		}
		else if (getpid() == pid[1])
		{
			kill(pid[0], SIGUSR2);
			kill(pid[2], SIGUSR2);
			kill(pid[1], SIGUSR2);
		}
		else if (getpid() == pid[2])
		{
			kill(pid[0], SIGUSR2);
			kill(pid[1], SIGUSR2);
			kill(pid[2], SIGUSR2);
		}
	}
}

void handle_s4(int sig)
{
	int message;
	if (read(fileDescriptor, &message, sizeof(message)) == ERROR)
		fprintf(stderr, "Couldn't read message from FIFO!\n");

	if (message == SIGUSR1)
		exit(EXIT_SUCCESS);
	else if (message == SIGURG)
		isPaused = TRUE;
	else if (message == SIGCONT)
		isPaused = FALSE;
}

void sem_signal(int semNumber, int id)
{
	struct sembuf sem;

	sem.sem_num = semNumber;
	sem.sem_op = 1;
	sem.sem_flg = 0;
	semop(id, &sem, 1);
}

void sem_wait(int semNumber, int id)
{
	struct sembuf sem;

	sem.sem_num = semNumber;
	sem.sem_op = -1;
	sem.sem_flg = 0;
	semop(id, &sem, 1);
}
