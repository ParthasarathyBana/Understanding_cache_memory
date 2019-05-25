/*
	Cache Simulator Implementation by Justin Goins
	Oregon State University
	Fall Term 2018
*/

#ifndef _CACHECONTROLLER_H_
#define _CACHECONTROLLER_H_

#include "CacheStuff.h"
#include <string>
#include <vector>
#include <mutex>
#include <list>
#include <atomic>
#include <condition_variable>

class CacheController {
	private:
		struct AddressInfo {
			unsigned long int tag;
			unsigned int setIndex;
		};
		unsigned int numByteOffsetBits;
		unsigned int numSetIndexBits;
		unsigned int globalCycles;
		unsigned int globalHits;
		unsigned int globalMisses;
		unsigned int globalEvictions;
		std::string inputFile, outputFile;
		std::vector<std::list<cache_block>> cache_structure;
		ConfigInfo ci;
		std::mutex *Accessbus;
		std::mutex *Localbus;
		std::mutex *locker;
		std::atomic<int> *thread_counter;
		std::condition_variable *cond;
		std::vector<std::list<unsigned long>> *invalidation_queue;
		int id;
		
		// function to allow read or write access to the cache
		void cacheAccess(CacheResponse*, bool, unsigned long int);
		// function that can compute the index and tag matching a specific address
		AddressInfo getAddressInfo(unsigned long int);
		// compute the number of clock cycles used to complete a memory access
		void updateCycles(CacheResponse*, bool);
		//to broadcast the invalidation_queue
		void Invalidate_caches(unsigned long int);
		//to invalidate cache
		void cache_block_invalidate();
	public:
		CacheController(int id, ConfigInfo, std::string, std::mutex*, std::mutex*, std::vector<std::list<unsigned long>>*, std::mutex*, std::atomic<int>*, std::condition_variable*);
		void runTracefile();
};

#endif //CACHECONTROLLER
