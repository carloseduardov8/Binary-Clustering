#ifndef MATRIX_H
#define MATRIX_H

#include "global.h"

typedef double data_t;

class Matrix {
    public:
		// Constructor:
        Matrix(int dimRows, int dimColumns);

		// Generates *n* elements, each containing *k* attributes:
		void generateRandom(bool parallel);

        // Retrieves a value:
        data_t get(int i, int j);

        // Saves a value:
        data_t put(data_t value, int i, int j);

		// Destructor:
        ~Matrix();


    protected:

		// Returns the boolean value of the i-th element:
        void fillLinesSerial();

		// Puts value in the i-th position:
        void fillLinesParallel(int threadId);

    private:
        data_t** matrix;
        int rows;
		int columns;
};

#endif // MATRIX_H