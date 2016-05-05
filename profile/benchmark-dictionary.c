
/*
 * 1. build netdata (as normally)
 * 2. cd profile/
 * 3. compile with:
 *    gcc -O3 -Wall -Wextra -I ../src/ -I ../ -o benchmark-dictionary benchmark-dictionary.c ../src/dictionary.o ../src/log.o ../src/avl.o ../src/common.o -pthread
 *
 */

#include <stdio.h>
#include <inttypes.h>

#include "dictionary.h"
#include "main.h"
#include "log.h"
#include "common.h"

struct myvalue {
	int i;
};

int main(int argc, char **argv) {
	if(argc || argv) {;}

	DICTIONARY *dict = dictionary_create(DICTIONARY_FLAG_SINGLE_THREADED);
	if(!dict) fatal("Cannot create dictionary.");

	struct rusage start, end;
	unsigned long long dt;
	char buf[100 + 1];
	struct myvalue value, *v;
	int i, max = 1000000, max2;

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Inserting %d entries in the dictionary\n", max);
	for(i = 0; i < max; i++) {
		value.i = i;
		snprintf(buf, 100, "%d", i);

		dictionary_set(dict, buf, &value, sizeof(struct myvalue));
	}
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Added %d entries in %llu nanoseconds: %llu inserts per second\n", max, dt, max * 1000000ULL / dt);
	fprintf(stderr, " > Dictionary: %llu inserts, %llu deletes, %llu searches\n\n", dict->inserts, dict->deletes, dict->searches);

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Retrieving %d entries from the dictionary\n", max);
	for(i = 0; i < max; i++) {
		value.i = i;
		snprintf(buf, 100, "%d", i);

		v = dictionary_get(dict, buf);
		if(!v)
			fprintf(stderr, "ERROR: cannot get value %d from the dictionary\n", i);
		else if(v->i != i)
			fprintf(stderr, "ERROR: expected %d but got %d\n", i, v->i);
	}
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Read %d entries in %llu nanoseconds: %llu searches per second\n", max, dt, max * 1000000ULL / dt);
	fprintf(stderr, " > Dictionary: %llu inserts, %llu deletes, %llu searches\n\n", dict->inserts, dict->deletes, dict->searches);

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Resetting %d entries in the dictionary\n", max);
	for(i = 0; i < max; i++) {
		value.i = i;
		snprintf(buf, 100, "%d", i);

		dictionary_set(dict, buf, &value, sizeof(struct myvalue));
	}
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Reset %d entries in %llu nanoseconds: %llu inserts per second\n", max, dt, max * 1000000ULL / dt);
	fprintf(stderr, " > Dictionary: %llu inserts, %llu deletes, %llu searches\n\n", dict->inserts, dict->deletes, dict->searches);

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Searching  %d non-existing entries in the dictionary\n", max);
	max2 = max * 2;
	for(i = max; i < max2; i++) {
		value.i = i;
		snprintf(buf, 100, "%d", i);

		v = dictionary_get(dict, buf);
		if(v)
			fprintf(stderr, "ERROR: cannot got non-existing value %d from the dictionary\n", i);
	}
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Searched %d non-existing entries in %llu nanoseconds: %llu searches per second\n", max, dt, max * 1000000ULL / dt);
	fprintf(stderr, " > Dictionary: %llu inserts, %llu deletes, %llu searches\n\n", dict->inserts, dict->deletes, dict->searches);

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Deleting %d entries from the dictionary\n", max);
	for(i = 0; i < max; i++) {
		value.i = i;
		snprintf(buf, 100, "%d", i);

		dictionary_del(dict, buf);
	}
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Deleted %d entries in %llu nanoseconds: %llu deletes per second\n", max, dt, max * 1000000ULL / dt);
	fprintf(stderr, " > Dictionary: %llu inserts, %llu deletes, %llu searches\n\n", dict->inserts, dict->deletes, dict->searches);

	// ------------------------------------------------------------------------

	getrusage(RUSAGE_SELF, &start);
	dict->inserts = dict->deletes = dict->searches = 0ULL;
	fprintf(stderr, "Destroying dictionary\n");
	dictionary_destroy(dict);
	getrusage(RUSAGE_SELF, &end);
	dt = (end.ru_utime.tv_sec * 1000000ULL + end.ru_utime.tv_usec) - (start.ru_utime.tv_sec * 1000000ULL + start.ru_utime.tv_usec);
	fprintf(stderr, "Destroyed in %llu nanoseconds\n", dt);

	return 0;
}
