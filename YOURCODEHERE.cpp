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

/* Some global variables to track heuristic progress.
 * 
 * Feel free to create more global variables to track progress of your
 * heuristic.
 */
#define KILOBYTE 1024


int order[15] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 12, 13, 14 };
int dimensionIndex = 0;
int choiceIndex = 0;
int traversalIndex = 0;
bool currentDimDone = false;
bool isDSEComplete = false;
unsigned int currentlyExploringDim = order[dimensionIndex];
std::string bestTraversalConfig = "";

/*
 * Given a half-baked configuration containing cache properties, generate
 * latency parameters in configuration string. You will need information about
 * how different cache paramters affect access latency.
 * 
 * Returns a string similar to "1 1 1"
 */

std::string generateCacheLatencyParams(string halfBackedConfig) {

    std::stringstream latencySettings;

	// L1 Data Cache Size and Associativity 
    int dl1Size = getdl1size(halfBackedConfig);
    unsigned int dl1Assoc = 1 << extractConfigPararm(halfBackedConfig, 4);

	// L1 Instruction Cache Size and Associativity 
    int il1Size = getil1size(halfBackedConfig);
    unsigned int il1Assoc = 1 << extractConfigPararm(halfBackedConfig, 6);

	// L2 Unified Cache Size and Associativity 
    int ul2Size = getl2size(halfBackedConfig);
    unsigned int ul2Assoc = 1 << extractConfigPararm(halfBackedConfig, 9);

	// Calculating latencies (without considering associativity)
    int dl1Lat = log2(dl1Size / KILOBYTE);
    int il1Lat = log2(il1Size / KILOBYTE);
    int ul2Lat = log2(ul2Size / KILOBYTE);
	
	/* DEBUG
    cout << "__________________________________________\n";
	cout << "dl1Size (KB): " << dl1Size / KILOBYTE << "\n";
	cout << "dl1Assoc: " << dl1Assoc << "\n";
	cout << "dl1Lat (initial): " << dl1Lat<< "\n\n";

    cout << "__________________________________________\n";
	cout << "il1Size (KB): " << il1Size / KILOBYTE << "\n";
	cout << "il1Assoc: " << il1Assoc << "\n";
	cout << "il1Lat (initial): " << il1Lat<< "\n\n";

    cout << "__________________________________________\n";
	cout << "ul2Size (KB): " << ul2Size / KILOBYTE << "\n";
	cout << "ul2Assoc: " << ul2Assoc << "\n";
	cout << "ul2Lat (initial): " << ul2Lat<< "\n\n";
	*/

	// Adding additional latency cycles due to associativity
	dl1Lat += log2(dl1Assoc);
	il1Lat += log2(il1Assoc);
	ul2Lat += log2(ul2Assoc); 
	 
	/* DEBUG
	cout << "dl1Lat (final): " << dl1Lat << "\n";
	cout << "il1Lat (final): " << il1Lat << "\n";
	cout << "ul2Lat (final): " << ul2Lat << "\n\n";
	*/

	// Conversions of latencies to corresponding indices as defined in runprojectsuite.sh
	int dl1LatIndex = dl1Lat - 1;
	int il1LatIndex = il1Lat - 1;
	int ul2LatIndex = ul2Lat - 5;

	/* DEBUG
	cout << "dl1LatIndex: " << dl1LatIndex << "\n";
	cout << "il1LatIndex: " << il1LatIndex << "\n";
	cout << "ul2LatIndex: " << ul2LatIndex << "\n\n";
	*/

    latencySettings << dl1LatIndex << " " << il1LatIndex << " " << ul2LatIndex;

    return latencySettings.str();
}

/*
 * Returns 1 if configuration is valid, else 0
 */
int validateConfiguration(std::string configuration) {

    // the BLOCK sizes of L1 instruction & data caches and unified cache
    unsigned int dl1BlockSize = 8 * (1 << extractConfigPararm(configuration, 2));
    unsigned int il1BlockSize = 8 * (1 << extractConfigPararm(configuration, 2));
    unsigned int ul2BlockSize = 16 << extractConfigPararm(configuration, 8);

    // The (OVERALL) sizes of L1 instruction & data caches and unified cache
    unsigned int dl1Size = getdl1size(configuration);
    unsigned int il1Size = getil1size(configuration);
    unsigned int ul2Size = getl2size(configuration);

    // instruction fetch queue
    int ifq = 1 << extractConfigPararm(configuration, 0);

    // Case 1: the L1 instruction cache block size must be at least the instruction fetch queue size 
    // Also, the L1 data cache should have the same block size as the L1 instruction cache
    if ((il1BlockSize < ifq) || (il1BlockSize != dl1BlockSize))
    	return 0;
    
    // Case 2: unified L2 cache block size must be at least twice the il1 block size 
    // but a max of 128 bytes
    // Also ul2 overall size must be at least twice the overall sizes of il1 + dl1
    if ( ((ul2BlockSize < (2 * il1BlockSize)) 
        || (ul2BlockSize > 128))
        || (ul2Size < 2 *(il1Size + dl1Size)) )
    	return 0;

    // Case 3: il1 and dl1 sizes must be a minimum of 2 KB and maximum of 64 KB
    if ((il1Size < 2*KILOBYTE) || (il1Size > 64*KILOBYTE))
    	return 0;
    if ((dl1Size < 2*KILOBYTE) || (dl1Size > 64*KILOBYTE))
        return 0;

    // Case 4: ul2 size must be a minimum of 32 KB and maximum of 1024 KB
    if ((ul2Size < 32*KILOBYTE) || (ul2Size > 1024*KILOBYTE))
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

	/*
    cout << "__________________________________________\n";
    cout << "PRIOR TO WHILE LOOP: \n";
    cout << "choiceIndex: " << choiceIndex << "\n";
    cout << "nextconfiguration: " << currentconfiguration << "\n";
    cout << "currentlyExploringDim: " << currentlyExploringDim << "\n";
    cout << "Dimension name: " << GLOB_dimensionnames[currentlyExploringDim] << "\n";
    cout << "__________________________________________\n";
    cout << "\n\n";
	*/

	std::string nextconfiguration = currentconfiguration;
	// Continue if proposed configuration is invalid or has been seen/checked before.
	while (!validateConfiguration(nextconfiguration) || GLOB_seen_configurations[nextconfiguration]) {
		
		/* Debugging print statements
        cout << "choiceIndex: " << choiceIndex << "\n";
        cout << "nextconfiguration: " << currentconfiguration << "\n";
        cout << "currentlyExploringDim: " << currentlyExploringDim << "\n";
        cout << "Dimension name: " << GLOB_dimensionnames[currentlyExploringDim] << "\n";
        cout << "\n";
		*/ 

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

		// choiceIndex holds the index for the value in the array for the current dimension
		// dimensionIndex holds the index of the current dimension in the order
		// currentlyExploringDim holds the value for the current dimension being explored
		
		if (choiceIndex >= GLOB_dimensioncardinality[currentlyExploringDim]) {
			choiceIndex = GLOB_dimensioncardinality[currentlyExploringDim] - 1;
			currentDimDone = true;
		} 

		ss << choiceIndex << " ";
		choiceIndex++;

		// Fill in remaining independent params with 0.
		for (int dim = (currentlyExploringDim + 1); dim < (NUM_DIMS - NUM_DIMS_DEPENDENT); ++dim) {
			ss << extractConfigPararm(bestConfig, dim) << " ";
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
			currentlyExploringDim = order[++dimensionIndex];
			choiceIndex = 0;
			currentDimDone = false;
		}

		// Creating cases to determine if the DSE is done or not
		if (dimensionIndex >= (NUM_DIMS - NUM_DIMS_DEPENDENT)) {
			if (traversalIndex == 0) { // First traversal 
				bestTraversalConfig = nextconfiguration;
				traversalIndex++;
				
				// Reset global variables
				dimensionIndex = 0;
				currentlyExploringDim = order[dimensionIndex];
				choiceIndex = 0;
				currentDimDone = false;
			} else {
				if (nextconfiguration == bestTraversalConfig) { // The best traversal has been ensured by consecutive runs producing same result
					isDSEComplete = true;
				} else {
					bestTraversalConfig = nextconfiguration;

					// Reset global variables
					dimensionIndex = 0;
					currentlyExploringDim = order[dimensionIndex];
					choiceIndex = 0;
					currentDimDone = false;
					traversalIndex++;
				}
			}
		}
	}
	return nextconfiguration;
}