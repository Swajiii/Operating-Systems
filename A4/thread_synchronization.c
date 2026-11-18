#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <time.h>

sem_t running;   // binary semaphore: guards the shared state below and the critical section
sem_t even;      // not needed by this solution (kept from skeleton)
sem_t odd;       // not needed by this solution (kept from skeleton)

void logStart(char *tID); //function to log that a new thread is started
void logFinish(char *tID); //function to log that a thread has finished its time

void startClock(); //function to start program clock
long getCurrentTime(); //function to check current time since clock was started
time_t programClock; //the global timer/clock for the program

typedef struct thread //represents a single thread, you can add more members if required
{
	char tid[4]; //id of the thread as read from file
	unsigned int startTime;
	int state;
	pthread_t handle;
	int retVal;
} Thread;

/* Shared synchronization state. Always read or written while holding 'running'. */
static Thread *allThreads = NULL;
static int totalThreads = 0;
static int firstIndex = -1;   // thread that must enter first (earliest creation)
static int csCount = 0;       // number of threads that have entered their critical section
static int lastParity = -1;   // parity (0 = even, 1 = odd) of the last thread in the critical section

int threadsLeft(Thread *threads, int threadCount);
int threadToStart(Thread *threads, int threadCount);
void* threadRun(void *t); //the thread function, the code executed by each thread
int readFile(char *fileName, Thread **threads); //function to read the file content and build array of threads

/* parity of a thread: the digit y of its ID txy (0 = even, 1 = odd) */
int parityOf(Thread *t) {
	return (t->tid[2] - '0') % 2;
}

/* number of threads of the given parity that have not finished yet (includes not-yet-started) */
int countUnfinished(int parity) {
	int n = 0;
	for (int k = 0; k < totalThreads; k++)
		if (allThreads[k].state != -1 && parityOf(&allThreads[k]) == parity)
			n++;
	return n;
}

/* true when every thread has already been created */
int allStarted() {
	for (int k = 0; k < totalThreads; k++)
		if (allThreads[k].state == 0)
			return 0;
	return 1;
}

/* ordering between two waiting threads: by ID when releasing starved threads,
   otherwise by creation time (start time, then position in the file) */
int earlier(Thread *a, Thread *b, int byId) {
	if (byId)
		return strcmp(a->tid, b->tid) < 0;
	if (a->startTime != b->startTime)
		return a->startTime < b->startTime;
	return (a - allThreads) < (b - allThreads);
}

/* Decides whether 'me' may enter its critical section now. Caller must hold 'running'. */
int canEnter(Thread *me) {
	// The first thread (by creation time) always goes first.
	if (csCount == 0)
		return (me - allThreads) == firstIndex;

	int eligible;
	int starving = 0;

	if (countUnfinished(1 - lastParity) > 0) {
		// Alternation: the next thread must have the opposite parity of the last one.
		eligible = 1 - lastParity;
	}
	else if (allStarted()) {
		// Starvation case: only threads of the last parity are left and no thread
		// can still arrive to help, so they are released in ID order.
		eligible = lastParity;
		starving = 1;
	}
	else {
		// Only threads of the last parity remain, but more threads are still to arrive.
		return 0;
	}

	if (parityOf(me) != eligible)
		return 0;

	// Among the started, unfinished threads of the eligible parity, pick the next one.
	Thread *best = NULL;
	for (int k = 0; k < totalThreads; k++) {
		Thread *c = &allThreads[k];
		if (c->state != 1 || parityOf(c) != eligible)
			continue;
		if (best == NULL || earlier(c, best, starving))
			best = c;
	}
	return best == me;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		printf("Input file name missing...exiting with error code -1\n");
		return -1;
	}

	sem_init(&running, 0, 1);

	Thread *threads = NULL;
	int threadCount = readFile(argv[1], &threads);

	// Share the thread list with the synchronization code and find the first thread
	// in creation order (earliest start time, ties broken by file position).
	allThreads = threads;
	totalThreads = threadCount;
	for (int k = 0; k < threadCount; k++)
		if (firstIndex == -1 || threads[k].startTime < threads[firstIndex].startTime)
			firstIndex = k;

	startClock();
	while (threadsLeft(threads, threadCount) > 0)
	{
		int i = 0;
		while ((i = threadToStart(threads, threadCount)) > -1) {
			sem_wait(&running);
			threads[i].state = 1;
			sem_post(&running);
			threads[i].retVal = pthread_create(&(threads[i].handle), NULL,
					threadRun, &threads[i]);
		}
	}

	for (int k = 0; k < threadCount; k++)
		if (threads[k].retVal == 0)
			pthread_join(threads[k].handle, NULL);

	sem_destroy(&running);
	return 0;
}

int readFile(char *fileName, Thread **threads) //do not modify this method
{
	FILE *in = fopen(fileName, "r");
	if (!in) {
		printf(
				"Child A: Error in opening input file...exiting with error code -1\n");
		return -1;
	}

	struct stat st;
	fstat(fileno(in), &st);
	char *fileContent = (char*) malloc(((int) st.st_size + 1) * sizeof(char));
	fileContent[0] = '\0';
	while (!feof(in)) {
		char line[100];
		if (fgets(line, 100, in) != NULL) {
			strncat(fileContent, line, strlen(line));
		}
	}
	fclose(in);

	char *command = NULL;
	int threadCount = 0;
	char *fileCopy = (char*) malloc((strlen(fileContent) + 1) * sizeof(char));
	strcpy(fileCopy, fileContent);
	command = strtok(fileCopy, "\r\n");
	while (command != NULL) {
		threadCount++;
		command = strtok(NULL, "\r\n");
	}
	*threads = (Thread*) malloc(sizeof(Thread) * threadCount);

	char *lines[threadCount];
	command = NULL;
	int i = 0;
	command = strtok(fileContent, "\r\n");
	while (command != NULL) {
		lines[i] = malloc(sizeof(command) * sizeof(char));
		strcpy(lines[i], command);
		i++;
		command = strtok(NULL, "\r\n");
	}

	for (int k = 0; k < threadCount; k++) {
		char *token = NULL;
		int j = 0;
		token = strtok(lines[k], ";");
		while (token != NULL) {
//if you have extended the Thread struct then here
//you can do initialization of those additional members
//or any other action on the Thread members
			(*threads)[k].state = 0;
			if (j == 0)
				strcpy((*threads)[k].tid, token);
			if (j == 1)
				(*threads)[k].startTime = atoi(token);
			j++;
			token = strtok(NULL, ";");
		}
	}
	return threadCount;
}

void logStart(char *tID) {
	printf("[%ld] New Thread with ID %s is started.\n", getCurrentTime(), tID);
}

void logFinish(char *tID) {
	printf("[%ld] Thread with ID %s is finished.\n", getCurrentTime(), tID);
}

int threadsLeft(Thread *threads, int threadCount) {
	int remainingThreads = 0;
	for (int k = 0; k < threadCount; k++) {
		if (threads[k].state > -1)
			remainingThreads++;
	}
	return remainingThreads;
}

int threadToStart(Thread *threads, int threadCount) {
	for (int k = 0; k < threadCount; k++) {
		if (threads[k].state == 0 && threads[k].startTime == getCurrentTime())
			return k;
	}
	return -1;
}

void* threadRun(void *t) //implement this function in a suitable way
{
	Thread *me = (Thread*) t;
	logStart(me->tid);

	// Entry protocol: wait until this thread is allowed to enter.
	sem_wait(&running);
	while (!canEnter(me)) {
		sem_post(&running);
		usleep(1000);
		sem_wait(&running);
	}

	//critical section starts here
	printf("[%ld] Thread %s is in its critical section\n", getCurrentTime(),
			me->tid);
	lastParity = parityOf(me);
	csCount++;
	//critical section ends here

	// Exit protocol: log, mark finished, then let the next thread proceed.
	logFinish(me->tid);
	me->state = -1;
	sem_post(&running);

	pthread_exit(0);
}

void startClock() {
	programClock = time(NULL);
}

long getCurrentTime() //invoke this method whenever you want check how much time units passed
//since you invoked startClock()
{
	time_t now;
	now = time(NULL);
	return now - programClock;
}