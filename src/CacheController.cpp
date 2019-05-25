/*
	Cache Simulator Implementation by Justin Goins
	Oregon State University
	Fall Term 2018
*/

#include "CacheController.h"
#include <iostream>
#include <fstream>
#include <regex>
#include <cmath>
#include <random> // these are used to generate a random integer number using the Marcenne Twister algorithm for the Random replacement policy
#include <chrono> //

using namespace std;

CacheController::CacheController(int id, ConfigInfo ci, string tracefile, mutex *Accessbus, mutex *Localbus, vector<list<unsigned long>> *invalidation_queue, mutex *locker, atomic<int> *thread_counter, condition_variable *cond) 
{
	// store the configuration info
	this->ci = ci;
	this->inputFile = string(tracefile);
	this->outputFile = this->inputFile + ".out";
	// compute the other cache parameters
	this->numByteOffsetBits = log2(ci.blockSize);
	this->numSetIndexBits = log2(ci.numberSets);
	// initialize the counters
	this->globalCycles = 0;
	this->globalHits = 0;
	this->globalMisses = 0;
	this->globalEvictions = 0;
	this->Accessbus = Accessbus;
	this->Localbus = Localbus;
	this->locker = locker;
	this->thread_counter = thread_counter;
	this->cond = cond;	
	this->id = id;
	this->invalidation_queue = invalidation_queue;
	// create your cache structure
	// ...
	
	this->cache_structure.resize(this->ci.numberSets);
	for(unsigned int i = 0; i < this->ci.numberSets; i++)// initialize the pointer to point the rows (indexes)
	{
		auto j = cache_structure[i].begin();
		for(unsigned int k = 0; k< this->ci.associativity; ++k) // initialize the pointer to point the columns (associativity)
		{
			//initialize the valid tag and dirty bit to 0
			cache_block datablock = 
			{	
				0, 
				0,
				0,
			};
			cache_structure[i].push_back(datablock);
			//cout << j->valid;
			j++;
		}	
		// cout << endl;
	}
}
//function to determine which of the caches have to be invalidated and broadcast the values from the cache that is invalidating the other caches
void CacheController::Invalidate_caches(unsigned long int address)
{
	this->Accessbus->lock();
	for(int i =0; i < 4; i++)
	{
		if(this->id!=i)
		{
			this->Localbus[i].lock();
			(*this->invalidation_queue)[i].push_back(address);
			this->Localbus[i].unlock();
		}
	}
	this->Accessbus->unlock();
}
//function to find the particular block in the cache to be invalidate and invalidate it 
void CacheController::cache_block_invalidate()
{
	this->Localbus[this->id].lock();
	// get index and tag of buffer by calling the getaddressinfo function using pass by value
	//cout << "in cache_block_invalidate" << endl;
	AddressInfo ai = getAddressInfo((*this->invalidation_queue)[this->id].front());
	
	for(auto j = cache_structure[ai.setIndex].begin(); j != cache_structure[ai.setIndex].end(); ++j)
	{
		if(ai.tag == j->tag)
		{
		//cout << "checking tag" << endl;
		j->valid = 0;
		// cout<<"invalidation successful"<<endl;
		} 
	}
	(*this->invalidation_queue)[this->id].pop_front();
	this->Localbus[this->id].unlock();
}
	// manual test code to see if the cache is behaving properly
	// will need to be changed slightly to match the function prototype
	/*
	cacheAccess(false, 0);
	cacheAccess(false, 128);
	cacheAccess(false, 256);

	cacheAccess(false, 0);
	cacheAccess(false, 128);
	cacheAccess(false, 256);
	*/


/*
	Starts reading the tracefile and processing memory operations.
*/
void CacheController::runTracefile() 
{
	cout << "Input tracefile: " << inputFile << endl;
	cout << "Output file name: " << outputFile << endl;
	
	// process each input line
	string line;
	// define regular expressions that are used to locate commands
	regex commentPattern("==.*");
	regex instructionPattern("I .*");
	regex loadPattern(" (L )(.*)(,[[:digit:]]+)$");
	regex storePattern(" (S )(.*)(,[[:digit:]]+)$");
	regex modifyPattern(" (M )(.*)(,[[:digit:]]+)$");

	// open the output file
	ofstream outfile(outputFile);
	// open the output file
	ifstream infile(inputFile);
	// parse each line of the file and look for commands
	while (getline(infile, line)) {
		// these strings will be used in the file output
		string opString, activityString;
		smatch match; // will eventually hold the hexadecimal address string
		unsigned long int address;
		// create a struct to track cache responses
		CacheResponse response;
		// ignore comments
		if (std::regex_match(line, commentPattern) || std::regex_match(line, instructionPattern)) {
			// skip over comments and CPU instructions
			//cout << "found a comment" << endl;
			continue;
		} else if (std::regex_match(line, match, loadPattern)) {
			//cout << "Found a load op!" << endl;
			istringstream hexStream(match.str(2));
			hexStream >> std::hex >> address;
			outfile << match.str(1) << match.str(2) << match.str(3);
			cacheAccess(&response, false, address);
			outfile << " " << response.cycles << (response.hit ? " hit" : " miss") << (response.eviction ? " eviction" : "");
		} else if (std::regex_match(line, match, storePattern)) {
			//cout << "Found a store op!" << endl;
			istringstream hexStream(match.str(2));
			hexStream >> std::hex >> address;
			outfile << match.str(1) << match.str(2) << match.str(3);
			cacheAccess(&response, true, address);
			outfile << " " << response.cycles << (response.hit ? " hit" : " miss") << (response.eviction ? " eviction" : "");
		} else if (std::regex_match(line, match, modifyPattern)) {
			//cout << "Found a modify op!" << endl;
			istringstream hexStream(match.str(2));
			hexStream >> std::hex >> address;
			outfile << match.str(1) << match.str(2) << match.str(3);
			// first process the read operation
			cacheAccess(&response, false, address);
			string tmpString; // will be used during the file output
			tmpString.append(response.hit ? " hit" : " miss");
			tmpString.append(response.eviction ? " eviction" : "");
			unsigned long int totalCycles = response.cycles; // track the number of cycles used for both stages of the modify operation
			// now process the write operation
			cacheAccess(&response, true, address);
			tmpString.append(response.hit ? " hit" : " miss");
			tmpString.append(response.eviction ? " eviction" : "");
			totalCycles += response.cycles;
			outfile << " " << totalCycles << tmpString;
		} else {
			throw runtime_error("Encountered unknown line format in tracefile.");
		}
		outfile << endl;
	}
	// add the final cache statistics
	outfile << "Hits: " << globalHits << " Misses: " << globalMisses << " Evictions: " << globalEvictions << endl;
	outfile << "Cycles: " << globalCycles << endl;

	infile.close();
	outfile.close();
	lock_guard<mutex> lk(*locker);
	(*this->thread_counter)--;
	this->cond->notify_all();
	}

/*
	Calculate the block index and tag for a specified address.
*/
CacheController::AddressInfo CacheController::getAddressInfo(unsigned long int address) {
	AddressInfo ai;
	// this code should be changed to assign the proper index and tag
	//cout << address;
	unsigned long int blockaddress = address / this->ci.blockSize;
	ai.setIndex = blockaddress % this->ci.numberSets;
	ai.tag = blockaddress / this->ci.numberSets;
	return ai;
}

/*
	This function allows us to read or write to the cache.
	The read or write is indicated by isWrite.
*/
void CacheController::cacheAccess(CacheResponse* response, bool isWrite, unsigned long int address) {
	// determine the index and tag
	AddressInfo ai = getAddressInfo(address);

	//cout << "\tSet index: " << ai.setIndex << ", tag: " << ai.tag << endl;
	
	// your code needs to update the global counters that track the number of hits, misses, and evictions
	// add your MESI protocol code here
	
	bool cache_line_full = true;
		// if block is in cache
		// if block is not in cache && empty space for new block
		// if block is not in cache & no free space & LRU
		// if block is not in cache & no free space & Random
		
		Invalidate_caches(address); // broadcasting the address

		for(auto j = cache_structure[ai.setIndex].begin(); j != cache_structure[ai.setIndex].end(); ++j)
		{
			
			if(j->valid == 1)
			{
				response->eviction = false;
				response->dirtyEviction = false;
				if(ai.tag == j->tag)
				{
					if(isWrite)
					{
						j->dirty = true;
					}
					response->hit = true;
					cache_line_full = false;
					cache_structure[ai.setIndex].splice(cache_structure[ai.setIndex].begin(), cache_structure[ai.setIndex], j);	
					break;
				}
				else
				{
					response->hit = false;
					
				}		
			}
		}
		for(auto k = cache_structure[ai.setIndex].begin(); k !=cache_structure[ai.setIndex].end();++k)
		{
			if(k->valid == 0)
			{
				response->hit = false;
				if(isWrite)
				{
					k->dirty = true;
				}
				(*k).tag = ai.tag;
				(*k).dirty = 0;
				(*k).valid = 1;
				cache_line_full = false;
				response->eviction = false;
				response->dirtyEviction = false;
				cache_structure[ai.setIndex].splice(cache_structure[ai.setIndex].begin(), cache_structure[ai.setIndex], k);
				break;
			}
		}
		if(cache_line_full == true && this->ci.rp == ReplacementPolicy::LRU)
		{	
			auto itr = cache_structure[ai.setIndex].end();
			response->hit = false;
			
			itr--;
			if(isWrite)
			{
				itr->dirty = true;
			}
			else
			{
				itr->dirty = false;
			}
			if(itr->dirty == true && isWrite)
			{
				response->dirtyEviction = true;
			}
			itr->tag = ai.tag;
			itr->valid = 1;
			response->eviction= true;
			cache_structure[ai.setIndex].splice(cache_structure[ai.setIndex].begin(), cache_structure[ai.setIndex], itr);	
		}
		else if(cache_line_full == true && this->ci.rp == ReplacementPolicy::Random)
		{
			auto seed = chrono::high_resolution_clock::now().time_since_epoch().count(); //This generates a pseudo random number using the Mersenne Twister algorithm.
			//Mersenne Twister algorithm is the most generally used algorithm to generate random numbers
			//Mersenne prime 2^19937 - 1
			mt19937 random_number(seed);
			int rand_num = random_number() % (this->ci.associativity); // this generates a randpom number in the range 0 and Rand_max
			//cout << "random number: " << rand_num << endl; 
			auto k = cache_structure[ai.setIndex].begin();
			for( int i = 0; i < rand_num; i++)
			{
				k++;
			}	
			response->hit = false;
			if(isWrite)
				{
					k->dirty = true;
				}
			else
			{
				k->dirty = false;
			}
			if(k->dirty == true && isWrite)
			{
				response->dirtyEviction = true;
			}
			k->tag = ai.tag;
			k->valid = 1;
			response->eviction = true;				
		}

	for(unsigned int i = 0; i < this->ci.numberSets; i++){
		for(auto j = cache_structure[i].begin(); j != cache_structure[i].end(); ++j){
			//cout << j->valid << " " << j->dirty << " " << j->tag << "\t";
		}
		//cout << endl;
	}
		
	if (response->hit)
	{
		this->globalHits++;
		//cout << "Address " << std::hex << address << " was a hit." << endl;

		//two cases: read and write
		//read: increment the hit counter and then pass the data values to cpu 
		//hits, evictions, dirtyevictions ...we are trying to figure out the number of cycles
		// in the case of a read...check if hit or miss...
		//if(hit){calculate the cycles needed to read values from cache, update hitcounter }
		//if(miss){calculate the cycles needed to read values from cache, read required data from memory, update cache and then again read the values from updated cache}

	}
	else
	{
		this->globalMisses++;
		//cout << "Address " << std::hex << address << " was a miss." << endl;
	}
	if(response->eviction)
	{
		this->globalEvictions++;
		// cout<< "Address " << std:: hex << address << " was an eviction."<< endl;
	}

	
	for( size_t i =0; i < (*this->invalidation_queue)[this->id].size(); i++)
	{
	cache_block_invalidate();	//invalidate caches
	}
	//(*this->invalidation_queue)[this->id].clear();
	updateCycles(response, isWrite);
	// cout << "-----------------------------------------" << endl;
	
	return;
}

/*
	Compute the number of cycles used by a particular memory operation.
	This will depend on the cache write policy.
*/
void CacheController::updateCycles(CacheResponse* response, bool isWrite)
{
	// your code should calculate the proper number of cycles
	// we used a tree diagram to understand each case and calculate the total cycles for each operation
	
	if(!isWrite) // for a read in write through -> hit/miss
	{
		if(response->dirtyEviction == true)
		{	
			response->cycles = this->ci.cacheAccessCycles + (2*this->ci.memoryAccessCycles);//x=number of read cycles to read from cache
		}
		else
		{
			if(response->hit)
			{
				response->cycles = this->ci.cacheAccessCycles;
			}
			else
			{
				response->cycles = this->ci.cacheAccessCycles + this->ci.memoryAccessCycles;//y=number of read cycles to read from main memory
			}
		}
	}
	else // for a write in write through -> hit/miss
	{
		if(this->ci.wp == WritePolicy::WriteThrough)
		{
			response->cycles = this->ci.cacheAccessCycles + this->ci.memoryAccessCycles;
		}
		else // for a write in write back -> hit/miss
		{
			if(response->dirtyEviction == true)
			{
				response->cycles = this->ci.cacheAccessCycles + this->ci.memoryAccessCycles;
			}
			else
			{
				response->cycles = this->ci.cacheAccessCycles;
			}
		}
		
	}
	this->globalCycles += response->cycles;
}