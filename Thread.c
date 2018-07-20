#define _GNU_SOURCE

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <sched.h>
#include <pthread.h>

static cpu_set_t all_cores(void)
{
	cpu_set_t cpuset;
	pthread_t this = pthread_self();
	pthread_getaffinity_np(this, sizeof(cpu_set_t), &cpuset);
	int numcores = sysconf(_SC_NPROCESSORS_ONLN);
	for (int id = 0; id < numcores; id++) {
		CPU_SET(id, &cpuset);
	}
	pthread_setaffinity_np(this, sizeof(cpu_set_t), &cpuset);
	return cpuset;
}

/**
* timespec_diff() - returns time difference in milliseconds for two timespecs.
* @stop:	time after event.
* @start:	time before event.
*
* Uses difftime() for time_t seconds calcultation.
*/
static double timespec_diff(struct timespec *stop, struct timespec *start)
{
	double diff = difftime(stop->tv_sec, start->tv_sec);
	diff *= 1000;
	diff += (stop->tv_nsec - start->tv_nsec) / 1e6;
	return diff;
}

struct thread_data {
	struct timespec start_time, end_time;
	double *arrptr;		/* Points to start of array slice */
	long long num_items;	/* Elements in slice */
	double *resptr;		/* Pointer to result */
	pthread_mutex_t *lock;	/* Lock for result */

};


/* Function runs in each thread */
void *threadfoo(void *args)
{
	/* Struct for datd, its type is known */
	struct thread_data *data = args;

	/* Checking time spent in each thread and the global time */
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &data->start_time);

	double res = 0.;
	for (int i = 0; i < data->num_items; i++){
		res += exp(data->arrptr[i]);		/* exhibitor */
		res += pov(data->arrptr[i]);		/* Random exponentiation */
		res += ldexp(data->arrptr[i],2);      	/* multiplying a floating-point number by an integer power of two */
		
	}

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &data->end_time);
	pthread_mutex_lock(data->lock); /* wait till acquire */
									
	*data->resptr += res;	/* manipulate the shared data */
	pthread_mutex_unlock(data->lock);      /* release lock for the others */

	return 0;
}
/* Random exponentiation */
int pov (double data)
{
	int y = rand(); 
	int res = pow (data, y);
	return res;
}

int main(int argc, char *argv[])
{
	int num_threads = 0;
	long long arr_size = 0;
	/* Parsing arguments */
	int argopt;
	/* "t:n:" means t,n switches, t & n require argument */
	while ((argopt = getopt(argc, argv, "t:n:")) != -1) {
		switch (argopt) {
		case 't':
			num_threads = atoi(optarg);
			break;
		case 'n':
			arr_size = atoll(optarg);
			break;
		default:
			fprintf(stderr, "Unknown option '%s'\n", optarg);
			exit(EXIT_FAILURE);
		}
	}
	/* There's some redundancy in arguments parsing */
	if (argc <= 1) {
		printf("To few arguments, add more\n");
		exit(0);
	}
	if (num_threads <= 0 || arr_size <= 0) {
		printf("Numbers of threads and array size should be >0\n");
		exit(0);
	}
	if (arr_size % num_threads) {
		printf("Numbers of threads is not a divisor of array size\n");
		exit(0);
	}

	pthread_t threads[num_threads];
	struct thread_data th_dat[num_threads];

	/* Fill array with randoms */
	FILE *fp_rand = fopen("/dev/random", "rb");
	if (NULL == fp_rand) {
		return 0;
	}
	unsigned int seed;
	fread(&seed, sizeof(seed), 1, fp_rand);
	if (ferror(fp_rand)) {
		return 0;
	}
	srand(seed);
	
	double *array = malloc(arr_size * sizeof *array);
	for (long long i = 0; i < arr_size; i++)
		array[i] = (5. / RAND_MAX) * rand();

	/* Configure thread flags */
	pthread_attr_t thread_attrs;
	pthread_attr_init(&thread_attrs); /* fill with default attributes */

									  
	pthread_attr_setschedpolicy(&thread_attrs, SCHED_FIFO);
	// Set maximum priority for main and other threads
	// As long as on Linux they compete for overall system resources
	pthread_setschedprio(pthread_self(), sched_get_priority_max(SCHED_FIFO));
	struct sched_param param;
	pthread_attr_getschedparam(&thread_attrs, &param);
	param.sched_priority = sched_get_priority_max(SCHED_FIFO);
	pthread_attr_setschedparam(&thread_attrs, &param);


	
	cpu_set_t cpuset = all_cores();
	int ret = pthread_attr_setaffinity_np(&thread_attrs, sizeof(cpu_set_t), &cpuset);
	if (ret < 0) {
		return -1;
	}


	/* Spawn threads */
	pthread_mutex_t sharedlock;
	pthread_mutex_init(&sharedlock, NULL);

	double result = 0.;
	struct timespec time_now, time_after;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time_now);
	for (int i = 0; i < num_threads; i++) {
		long long slice = arr_size / num_threads;
		th_dat[i].arrptr = &(array[i * slice]);	/* Points to start of array slice */
		th_dat[i].num_items = slice;		/* Elements in slice */
		th_dat[i].resptr = &result;		/* Pointer to result(shared) */
		th_dat[i].lock = &sharedlock;		/* Lock for result */
		pthread_create(&threads[i], &thread_attrs,
			&threadfoo, &th_dat[i]);
	}
	for (int i = 0; i < num_threads; i++)
		pthread_join(threads[i], NULL);

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &time_after);

	/* Resulting times */
	double took_global = timespec_diff(&time_after, &time_now);
	double took_avg = 0.;
	for (int i = 0; i < num_threads; i++) {
		took_avg += timespec_diff(&(th_dat[i].end_time),
			&(th_dat[i].start_time));
	}
	took_avg /= num_threads;

	printf("Numbers: %lld\nThreads: %d\nResult: %g\n"
		"Average thread time, ms: %g\nCalculation took, ms: %g\n",
		arr_size, num_threads, result, took_avg, took_global);

	pthread_mutex_destroy(&sharedlock);
	free(array);

}