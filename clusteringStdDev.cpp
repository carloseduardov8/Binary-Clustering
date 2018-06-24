#include "global.h"         // Global settings
#include <iostream>			// Formatted output
#include <ctime>			// For stopwatch
#include <cmath>            // Math routines
#include <thread>			// To parallelize computation
#include <limits>           // To use infinity
#include <iomanip>			// For printing arrays
#include "Bitmask.h"		// Array class for storing bits
#include "Matrix.h"			// Data matrix class
#include <unistd.h>

using namespace std;


int* powArr;

// Function declarations:
void binaryClustering(Matrix* matrix);
void findCentroids(int threadId, data_t* centroids, data_t* stdDev, Matrix* matrix);
void clusterSplitting(int threadId, Matrix* boundaries, Matrix* matrix);
void printArray(data_t* arr, int size);


// Main program:
int main(){

	// Loads the data matrix:
	Matrix data("/home/cemarciano/Documents/fullDataset.txt", true);

    // Starts the stopwatch:
	struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
    cout << "Start!" << endl;

    // Runs binary clustering algorithm:
    binaryClustering(&data);

	data.print(0, 20);

	// Stops the stopwatch:
	clock_gettime(CLOCK_MONOTONIC, &finish);

	// CHECKING MAX CLUSTER, PLS REMOVE LATER
	{
		int max = 0;
		int cluster = -1;
		for (int i=0; i<pow(K, data.getDims()); i++){
			if (data.getSignalDist(i)*1245005 > max){
				max = data.getSignalDist(i)*1245005;
				cluster = i;
			}
		}
		cout << endl << "Most signal elements: " << max << " elements in cluster " << cluster << endl;
		cout << "Background registers in this same cluster are " << data.getBackgroundDist(cluster)*63981 << " in total"<< endl << endl;
		max = 0;
		cluster = -1;
		for (int i=0; i<pow(K, data.getDims()); i++){
			if (data.getBackgroundDist(i)*63981 > max){
				max = data.getBackgroundDist(i)*63981;
				cluster = i;
			}
		}
		cout << endl << "Most background elements: " << max << " elements in cluster " << cluster << endl;
		cout << "Signal registers in this same cluster are " << data.getSignalDist(cluster)*1245005 << " in total"<< endl << endl;
	}

	// Prints out elapsed time:
    double elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
	cout << endl << "Elapsed time: " << elapsed << " seconds." << endl;


}



void binaryClustering(Matrix* matrix){

    /****************************/
    /*** CENTROID CALCULATION ***/
    /****************************/

    // Allocates centroid array:
    data_t* centroids = new data_t[ matrix->getDims() ]();
	// Allocates stddev array:
    data_t* stdDev = new data_t[ matrix->getDims() ]();

    // Array of threads:
	thread centroidTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to fill matrix:
		centroidTasks[threadId] = thread(findCentroids, threadId, centroids, stdDev, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		centroidTasks[threadId].join();
	}

    // Prints centroid values:
	cout << endl << "Centroid vector:";
    printArray(centroids, matrix->getDims());

	// Prints stddev values:
	cout << endl << "StdDev vector:";
    printArray(stdDev, matrix->getDims());


	/***************************/
    /*** DIVISION BOUNDARIES ***/
    /***************************/

	// Creates matrix D-DIMENSIONS by K-DIVISIONS:
	Matrix boundaries(matrix->getDims(), K);

	// Step to divide boundaries with:
	float step;
	// Checks how to divide the space:
	if (K % 2 == 0){
		// Calculates initial step:
		step = -1*floor((K - 1)/2);
	} else {
		// Calculates initial step:
		step = -1*(1.0*(K - 2)/2);
	}
	// Runs through dimensions of matrix:
	for (int i = 0; i < matrix->getDims(); i++){
		// Runs through each division:
		for (int j = 0; j < K-1; j++){
			// Saves the value of the boundary:
			boundaries.put(i, j, (WARP*(step+j)*stdDev[i])+centroids[i]);
		}
        // Adds infinity as last boundary:
        boundaries.put(i, K-1, numeric_limits<data_t>::infinity());
	}



    /*************************/
    /*** CLUSTER SPLITTING ***/
    /*************************/


    // Array containing future indexes of clusters:
    powArr = new int[ matrix->getDims() ];
    // Fills the array with powers of the number of divisions:
    for (int i = 0; i < matrix->getDims(); i++){
        powArr[i] = pow(K,i);
    }


    // Array of threads:
	std::thread splittingTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to fill matrix:
		splittingTasks[threadId] = thread(clusterSplitting, threadId, &boundaries, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		splittingTasks[threadId].join();
	}


}


// Job to calculate the centroid for each dimension:
void findCentroids(int threadId, data_t* centroids, data_t* stdDev, Matrix* matrix){

    // Number of columns to sum:
	float each = (matrix->getDims())*1.0 / CORES;

    // Calculates chunck:
    int start = round(threadId*each);
    int end = round((threadId+1)*each);

    // Loops through designated columns:
	for (int j = start; j < end; j++){

		// CENTROID:

        // Accumulator:
        data_t acc = 0;
        // Loops through lines:
		for (int i = 0; i < matrix->getRows(); i++){
            acc += matrix->get(i, j);
        }
        // Writes accumulator mean:
        centroids[j] = acc / matrix->getRows();

		// STDDEV:

		// Accumulator:
        acc = 0;
		// Current centroid:
		data_t currCentroid = centroids[j];
        // Loops through lines:
		for (int i = 0; i < matrix->getRows(); i++){
            acc += pow( matrix->get(i, j) - currCentroid, 2);
        }
        // Writes accumulator stddev:
        stdDev[j] = sqrt(acc/matrix->getRows());
    }

}



// Job to assign a cluster number to each register:
void clusterSplitting(int threadId, Matrix* boundaries, Matrix* matrix){

    // Number of lines to check:
	float each = (matrix->getRows())*1.0 / CORES;

    // Calculates chunck:
    int start = round(threadId*each);
    int end = round((threadId+1)*each);

    // Loops through designated lines:
	for (int i = start; i < end; i++){
        // Memory position accumulator:
        int memAcc = 0;
        // Loops through columns:
        for (int j = 0; j < matrix->getDims(); j++){
            // Loops through boundaries:
            for (int k = 0; k < K; k++){
                // Checks if data is to the left of boundary:
                if (matrix->get(i, j) < boundaries->get(j,k)){
                    // Moves the memory position further (sets 1 in bitmask, represented in 10s):
                    memAcc += powArr[j]*k;
                    break;
                }
            }
        }

        // Assigns a cluster to the data:
        matrix->putClusterOf(i, memAcc);
    }
}


// Prints the content of a given array:
void printArray(data_t* arr, int size){
	cout << endl << "[";
	for (int i = 0; i < size; i++){
        cout << setw(10) << arr[i];
		if (i != size-1) cout << '\t';
    }
	cout << "]" << endl;
}
