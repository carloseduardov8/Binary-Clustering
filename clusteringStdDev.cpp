#include "global.h"         // Global settings
#include <iostream>			// Formatted output
#include <ctime>			// For stopwatch
#include <cmath>            // Math routines
#include <thread>			// To parallelize computation
#include <limits>           // To use infinity
#include <iomanip>			// For printing arrays
#include "Bitmask.h"		// Array class for storing bits
#include "Matrix.h"			// Data matrix class
#include "SharedVector.h"	// Vector to be used across multiple threads
#include "SVM_Trainer.h"	// Performs SVM
#include <fstream> 			// Handles file operations
#include <unistd.h>

using namespace std;


typedef struct {
	int clusterId;
	vector<int> registers;
} clusterStruct;


int* powArr;					// Array to hold pre-calculated powers in base K

// Function declarations:
Bitmask* binaryClustering(Matrix* matrix);
void findCentroids(int threadId, data_t* centroids, data_t* stdDev, Matrix* matrix);
void clusterSplitting(int threadId, Matrix* boundaries, SharedVector<int>** clusterPtrs, Matrix* matrix);
void checkContamination(int threadId, Matrix* matrix);
void pickSupportVectors(int threadId, SharedVector<int>** clusterPtrs, struct svm_parameter param, Bitmask* chosen, Matrix* matrix);
void pickRegisters(int threadId, Bitmask* chosen, Matrix* matrix);
void printArray(data_t* arr, int size);
struct svm_parameter setSVMParams();


// Main program:
int main(){

	// Loads the data matrix:
	Matrix data("/home/cemarciano/Documents/fullDataset.txt", true);

    // Starts the stopwatch:
	struct timespec start, finish;
    clock_gettime(CLOCK_MONOTONIC, &start);
    cout << "Start!" << endl;

    // Runs binary clustering algorithm:
    Bitmask* chosen = binaryClustering(&data);

	// Stops the stopwatch:
	clock_gettime(CLOCK_MONOTONIC, &finish);

	// Prints part of the data matrix so we can look at it since it's so pretty:
	data.print(0, 20);

	// Prints how many registers were chosen:
	cout.imbue(std::locale(""));
	cout << endl << "Total registers chosen: " << chosen->getSize() << endl;
	cout << "This represents " << setprecision(4) << (chosen->getSize()*1.0/data.getRows())*100 << "% of the previous " << data.getRows() << " registers." << endl;

	// Saves the chosen array to file:
	ofstream myFile;
    myFile.open("/home/cemarciano/Documents/chosen.txt");
    // Writes to file:
    for (int i = 1; i <= chosen->getLength(); i++){
        if (chosen->get(i) == true){
            myFile << "1" << endl;
        } else {
			myFile << "0" << endl;
		}
    }
    myFile.close();


	// Prints out elapsed time:
    double elapsed = (finish.tv_sec - start.tv_sec);
    elapsed += (finish.tv_nsec - start.tv_nsec) / 1000000000.0;
	cout << endl << "Elapsed time: " << elapsed << " seconds." << endl << endl;


}



Bitmask* binaryClustering(Matrix* matrix){

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
	// PLEASE CHANGE LATER TO -1*((K/2) - 1)
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

	// Prints boundaries matrix:
	cout << endl << "Boundaries:";
	boundaries.print(0, 10);



    /*************************/
    /*** CLUSTER SPLITTING ***/
    /*************************/


    // Array containing future indexes of clusters (in base K):
    powArr = new int[ matrix->getDims() ];
    // Fills the array with powers of the number of divisions:
    for (int i = 0; i < matrix->getDims(); i++){
        powArr[i] = pow(K,i);
    }


	// Creates one shared vector pointer for each cluster.
	// A position in this array is a shared vector containing the IDs of the registers in that cluster:
	int totalClusters = pow(K,matrix->getDims());
	SharedVector<int>** clusterPtrs = new SharedVector<int>*[totalClusters]();



    // Array of threads:
	std::thread splittingTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to split the space:
		splittingTasks[threadId] = thread(clusterSplitting, threadId, &boundaries, clusterPtrs, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		splittingTasks[threadId].join();
	}

	// Saves cluster distribution:
	matrix->saveClusterDist();


    /***************************************/
    /*** CHECK CONTAMINATION OF CLUSTERS ***/
    /***************************************/

	// Array of threads:
	std::thread contaminationTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to check contamination:
		contaminationTasks[threadId] = thread(checkContamination, threadId, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		contaminationTasks[threadId].join();
	}


	/*******************/
    /*** SVM PICKING ***/
    /*******************/

	// Allocates bitmask to hold which registers were chosen:
	Bitmask* chosen = new Bitmask(matrix->getRows());

	// Sets SVM parameters:
	struct svm_parameter param = setSVMParams();

	// Array of threads:
	std::thread SVMTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to perform SVM tasks:
		SVMTasks[threadId] = thread(pickSupportVectors, threadId, clusterPtrs, param, chosen, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		SVMTasks[threadId].join();
	}

	/**********************/
    /*** RANDOM PICKING ***/
    /**********************/


	// Array of threads:
	std::thread pickerTasks[CORES];

	// Loops through threads:
	for (int threadId = 0; threadId < CORES; threadId++){
		// Fires up thread to fill matrix:
		pickerTasks[threadId] = thread(pickRegisters, threadId, chosen, matrix);
	}

	// Waits until all threads are done:
	for (int threadId = 0; threadId < CORES; threadId++){
		pickerTasks[threadId].join();
	}


	// Deletes allocated space:
	delete[] centroids;
	delete[] stdDev;
	delete[] powArr;

	// Returns chosen data:
	return chosen;

}


// Job to calculate the centroid for each dimension:
void findCentroids(int threadId, data_t* centroids, data_t* stdDev, Matrix* matrix){

    // Number of columns to sum:
	double each = (matrix->getDims())*1.0 / CORES;

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
void clusterSplitting(int threadId, Matrix* boundaries, SharedVector<int>** clusterPtrs, Matrix* matrix){

    // Number of lines to check:
	double each = (matrix->getRows())*1.0 / CORES;

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
                if (matrix->get(i, j) < boundaries->get(j, k)){
                    // Moves the memory position further (sets 1 in bitmask, represented in 10s):
                    memAcc += powArr[j]*k;
                    break;
                }
            }
        }

        // Assigns a cluster to the data:
        matrix->putClusterOf(i, memAcc);

		// Inserts data into corresponding cluster.
		// Checks if cluster is empty:
		if (clusterPtrs[memAcc] == NULL){
			// Allocates space for this cluster:
			clusterPtrs[memAcc] = new SharedVector<int>(CORES);
		}
		// Pushes register to cluster:
		(clusterPtrs[memAcc])->push(i, threadId);
    }
}


// Job to assign a cluster number to each register:
void checkContamination(int threadId, Matrix* matrix){

	// Number of chuncks to check:
	double each = (pow(K, matrix->getDims()))*1.0 / CORES;
    // Calculates chunck:
    int start = round(threadId*each);
    int end = round((threadId+1)*each);

	// Loops through designated clusters:
	for (int i = start; i < end; i++){

		// Retrieves total signal and background present in this cluster:
		int currentSignal = matrix->getSignalDist(i);
		int currentBackground = matrix->getBackgroundDist(i);

		// Puts both classes in the same scale of comparison (num in cluster / total registers of class):
		double signalFraction = currentSignal*1.0 / matrix->getSignalSize();
		double backgroundFraction = currentBackground*1.0 / matrix->getBackgroundSize();

		// Sets which class contamines this cluster the most:
		if (signalFraction >= backgroundFraction){
			// Sets cluster i as contamined by signal (class 0):
			matrix->putContamination(i, false);
		} else {
			// Sets cluster i as contamined by background (class 1):
			matrix->putContamination(i, true);
		}

		// Checks if this cluster has at least one register of each class:
		if ((signalFraction > 0) && (backgroundFraction > 0)) {
			// Sets cluster as having at least one register of each class:
			matrix->putHasBothClasses(i, true);
		}

		// Calculates a percentage of how much of this cluster should be retained:

		// Calculates the value corresponding to 100%:
		double totalFraction = signalFraction + backgroundFraction;
		// Prevents division by 0:
		if (totalFraction == 0) totalFraction = 1;
		// Takes either the % of signal, the % of background or the baseline minimum % defined in the global header:
		double selectedPercentage = max( min(signalFraction*1.0/totalFraction, backgroundFraction*1.0/totalFraction), PERC_MIN );

		// Calculates and saves individual class yields:

		// Calculates total registers in this cluster:
		int clusterSize = currentSignal + currentBackground;
		// Obtains the total number of registers this cluster will yield, taking into account global percentage multiplier:
		clusterSize *= (selectedPercentage*PERC_MULT);
		// Calculates individual class yields within this cluster:
		currentSignal = ( (signalFraction*1.0/totalFraction) * clusterSize );
		currentBackground = ( (backgroundFraction*1.0/totalFraction) * clusterSize ); //Alternative: clusterSize - currentSignal;

		// Checks if minimum number of selected registers is being respected:
		if ((currentSignal < TAKE_AT_LEAST) && (currentBackground < TAKE_AT_LEAST)){
			// Takes a minimum number of registers from each cluster:
			currentSignal = TAKE_AT_LEAST;
			currentBackground = TAKE_AT_LEAST;
		}
		// Saves available quantity of registers:
		matrix->putSignalDist(i, currentSignal);
		matrix->putBackgroundDist(i, currentBackground);
	}

}


// Job to perform SVM and retain support vectors:
void pickSupportVectors(int threadId, SharedVector<int>** clusterPtrs, struct svm_parameter param, Bitmask* chosen, Matrix* matrix){

	// Number of chuncks to check:
	double each = (pow(K, matrix->getDims()))*1.0 / CORES;
    // Calculates chunck:
    int start = round(threadId*each);
    int end = round((threadId+1)*each);

	// Loops through designated clusters:
	for (int cluster = start; cluster < end; cluster++){
		// Checks if cluster is eligible for SVM task (i.e. has at least one register of both classes):
		if (matrix->getHasBothClasses(cluster) == true){
			// Fires up SVM:
			SVM_Trainer result(matrix, clusterPtrs[cluster], param);
			// Retrieves each support vector:
			for (int i = 0; i < result.getTotalSV(); i++){
				// Gets register index:
				int regId = result.getSV(i);
				// Marks register as chosen:
				chosen->put(regId+1, true);
				// Reduces total available registers to be picked according to class:
				if (matrix->getClassOf(regId) == 0){
					// Gets how many more signal registers this cluster can still yield:
					int yield = matrix->getSignalDist(cluster);
					// Subtracts signal yield for this cluster, effectively "taking" one register:
					matrix->putSignalDist(cluster, yield-1);
				} else {
					// Gets how many more background registers this cluster can still yield:
					int yield = matrix->getBackgroundDist(cluster);
					// Subtracts background yield for this cluster, effectively "taking" one register:
					matrix->putBackgroundDist(cluster, yield-1);
				}
			}
		}
	}

}

// Job to pick which registers should be kept:
void pickRegisters(int threadId, Bitmask* chosen, Matrix* matrix){

    // Number of lines to check:
	double each = (matrix->getRows())*1.0 / CORES;

    // Calculates chunck:
    int start = round(threadId*each);
    int end = round((threadId+1)*each);

    // Loops through designated lines:
	for (int i = start; i < end; i++){

		// Retrieves cluster of current register:
		int cluster = matrix->getClusterOf(i);

		// Checks this register belongs to class 0 (signal):
		if (matrix->getClassOf(i) == 0){
			// Gets how many more signal registers this cluster can still yield:
			int yield = matrix->getSignalDist(cluster);
			// Checks if given cluster can still yield signal registers:
			if (yield > 0){
				// Subtracts signal yield for this cluster, effectively "taking" one register:
				matrix->putSignalDist(cluster, yield-1);
				// Marks register i as chosen:
				chosen->put(i+1, true);
			}
		// Case where register belongs to class 1 (background):
		} else {
			// Gets how many more background registers this cluster can still yield:
			int yield = matrix->getBackgroundDist(cluster);
			// Checks if given cluster can still yield background registers:
			if (yield > 0){
				// Subtracts background yield for this cluster, effectively "taking" one register:
				matrix->putBackgroundDist(cluster, yield-1);
				// Marks register i as chosen:
				chosen->put(i+1, true);
			}
		}

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


// Set all default parameters for param struct:
struct svm_parameter setSVMParams(){
	// Decares struct:
	struct svm_parameter param;
	// Sets parameters:
	param.svm_type = C_SVC;
	param.kernel_type = LINEAR;
	param.degree = 3;
	param.gamma = 1;	// 1/num_features
	param.coef0 = 0;
	param.nu = 0.5;
	param.cache_size = 100;
	param.C = 100;
	param.eps = 1e-3;
	param.p = 0.1;
	param.shrinking = 1;
	param.probability = 0;
	param.nr_weight = 0;
	param.weight_label = NULL;
	param.weight = NULL;
	// Returns struct:
	return param;
}
