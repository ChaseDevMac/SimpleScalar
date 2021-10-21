#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <fstream>
#include <map>
#include <math.h>
#include <fcntl.h>
#include <vector>
#include <iterator>

#include "431project.h"

using namespace std;

/*
 * Enter your PSU IDs here to select the appropriate scanning order.
 */
#define PSU_ID_SUM (929254193 + 940701210)

/*
 * Some global variables to track heuristic progress.
 * 
 * Feel free to create more global variables to track progress of your
 * heuristic.
 */
unsigned int currentlyExploringDim = 0;
bool currentDimDone = false;
bool isDSEComplete = false;

/*
 * Given a half-baked configuration containing cache properties, generate
 * latency parameters in configuration string. You will need information about
 * how different cache paramters affect access latency.
 * 
 * Returns a string similar to "1 1 1"
 */

std::string generateCacheLatencyParams(string halfBackedConfig) {

    std::stringstream latencySettings;

    // The (OVERALL) sizes of L1 instruction & data caches and unified cache
    int dl1 = getdl1size(halfBackedConfig);
    int il1 = getil1size(halfBackedConfig);
    int ul2 = getl2size(halfBackedConfig);

    // The cache latencies which is the log2 of the cache sizes
    // The sizes are in terms of KB but need it in terms of bytes (1 KB = 1024 bytes)
    int dl1lat = log2(dl1/1024);
    int il1lat = log2(il1/1024);
    int ul2lat = log2(ul2/1024);

    // get the associations (2-way, 4-way, 8-way) of the caches
    unsigned int dl1assoc = 1 << extractConfigPararm(halfBackedConfig, 4);
    unsigned int il1assoc = 1 << extractConfigPararm(halfBackedConfig, 6);
    unsigned int ul2assoc = 1 << extractConfigPararm(halfBackedConfig, 9);

    // The associations are a power of 2 so the additional cycles are the log2 of that 
    dl1lat += log2(dl1assoc);
    il1lat += log2(il1assoc);
    ul2lat += log2(ul2assoc);

    // latency settings is in the form "x y z"
    latencySettings << dl1lat << " " << il1lat << " " << ul2lat;

    return latencySettings.str();
}

/*
 * Returns 1 if configuration is valid, else 0
 */
int validateConfiguration(std::string configuration) {

    // The (OVERALL) sizes of L1 instruction & data caches and unified cache
    unsigned int dl1 = getdl1size(configuration);
    unsigned int il1 = getil1size(configuration);
    unsigned int ul2 = getl2size(configuration);
    
    // the BLOCK sizes of L1 instruction & data caches and unified cache
    unsigned int dl1blocksize = 8 * (1 << extractConfigPararm(configuration, 2));
    unsigned int il1blocksize = 8 * (1 << extractConfigPararm(configuration, 2));
    unsigned int ul2blocksize = 16 << extractConfigPararm(configuration, 8);


    // instruction fetch queue
    int ifq = 1 << extractConfigPararm(configuration, 0);

    // Case 1: the L1 instruction cache block size must be at least the instruction fetch queue size 
    // Also, the L1 data cache should have the same block size as the L1 instruction cache
    if ((il1blocksize < ifq) || (il1blocksize != dl1blocksize))
       return 0;

    // Case 2: unified L2 cache block size must be at least twice the il1 block size 
    // but a max of 128 bytes
    // Also ul2 overall size must be at least twice the overall sizes of il1 + dl1
    if ( (ul2blocksize < (2 * il1blocksize))
        || (ul2blocksize > 128)
        || (ul2 < 2 *(il1 + dl1)) )
       return 0;

    // Case 3: il1 and dl1 sizes must be a minimum of 2 KB and maximum of 64 KB
    if ((il1 < 2) || (il1 > 64))
        return 0;
    if ((dl1 < 2) || (dl1 > 64))
        return 0;

    // Case 4: ul2 size must be a minimum of 32 KB and maximum of 1024 KB
    if ((ul2 < 32) || (ul2 > 1028))
        return 0;


    // The below is a necessary, but insufficient condition for validating a
    // configuration.
    return isNumDimConfiguration(configuration);
}

/*
 * Given the current best known configuration, the current configuration,
 * and the globally visible map of all previously investigated configurations,
 * suggest a previously unexplored design point. You will only be allowed to
 * investigate 1000 design points in a particular run, so choose wisely.
 *
 * In the current implementation, we start from the leftmost dimension and
 * explore all possible options for this dimension and then go to the next
 * dimension until the rightmost dimension.
 */

std::string generateNextConfigurationProposal(std::string currentconfiguration,
		std::string bestEXECconfiguration, std::string bestEDPconfiguration,
		int optimizeforEXEC, int optimizeforEDP) {

        // Exploration order:
        // Cache: Start at index 2 (3rd argument) and iterate to 10 (11th argument)
        // FPU: Then go to index 11 
        // Core: index 0 (1st parameter) to 1 (2nd argument) 
        // Branch: index 12 (13rd arugment) to index 14 (15th argument)
	
	// Some interesting variables in 431project.h include:
	//
	// 1. GLOB_dimensioncardinality
	// 2. GLOB_baseline
	// 3. NUM_DIMS
	// 4. NUM_DIMS_DEPENDENT
	// 5. GLOB_seen_configurations

	std::string nextconfiguration = currentconfiguration;
	// Continue if proposed configuration is invalid or has been seen/checked before.
	while (!validateConfiguration(nextconfiguration) ||
		GLOB_seen_configurations[nextconfiguration]) {

		// Check if DSE has been completed before and return current
		// configuration.
		if(isDSEComplete) {
			return currentconfiguration;
		}

		std::stringstream ss;

		string bestConfig;
		if (optimizeforEXEC == 1)
			bestConfig = bestEXECconfiguration;

		if (optimizeforEDP == 1)
			bestConfig = bestEDPconfiguration;

		// Fill in the dimensions already-scanned with the already-selected best
		// value.
		for (int dim = 0; dim < currentlyExploringDim; ++dim) {
			ss << extractConfigPararm(bestConfig, dim) << " ";
		}

		// Handling for currently exploring dimension. This is a very dumb
		// implementation.
		int nextValue = extractConfigPararm(nextconfiguration,
				currentlyExploringDim) + 1;

		if (nextValue >= GLOB_dimensioncardinality[currentlyExploringDim]) {
			nextValue = GLOB_dimensioncardinality[currentlyExploringDim] - 1;
			currentDimDone = true;
		}

		ss << nextValue << " ";

		// Fill in remaining independent params with 0.
		for (int dim = (currentlyExploringDim + 1); dim < (NUM_DIMS - NUM_DIMS_DEPENDENT); ++dim) {
			ss << "0 ";
		}

		//
		// Last NUM_DIMS_DEPENDENT3 configuration parameters are not independent.
		// They depend on one or more parameters already set. Determine the
		// remaining parameters based on already decided independent ones.
		//
		string configSoFar = ss.str();

		// Populate this object using corresponding parameters from config.
		ss << generateCacheLatencyParams(configSoFar);

		// Configuration is ready now.
		nextconfiguration = ss.str();

		// Make sure we start exploring next dimension in next iteration.
		if (currentDimDone) {
			currentlyExploringDim++;
			currentDimDone = false;
		}

		// Signal that DSE is complete after this configuration.
		if (currentlyExploringDim == (NUM_DIMS - NUM_DIMS_DEPENDENT))
			isDSEComplete = true;
	}
	return nextconfiguration;
}
