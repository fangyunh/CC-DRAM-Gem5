#include <iostream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <random>

using namespace std;

void random_read(int *data, size_t num_elements, size_t num_reads, long long &sum, unsigned seed) {
    mt19937 rng(seed);
    uniform_int_distribution<size_t> dist(0, num_elements - 1);

    for (size_t i = 0; i < num_reads; ++i) {
        size_t idx = dist(rng);
        sum += data[idx];
    }
}

int main(int argc, char *argv[])
{
    size_t data_size = 1ULL * 1024 * 1024 * 512; // Default to 512MB
    size_t num_elements;

    if (argc == 1) {
        // Use default data_size
    } else if (argc == 2) {
        data_size = atol(argv[1]);
        if (data_size <= 0) {
            cerr << "Usage: " << argv[0] << " [data_size_in_bytes]" << endl;
            return 1;
        }
    } else {
        cerr << "Usage: " << argv[0] << " [data_size_in_bytes]" << endl;
        return 1;
    }

    num_elements = data_size / sizeof(int);

    cout << "Allocating " << data_size << " bytes (" << num_elements << " integers)" << endl;

    // Allocate array
    int *data = new(nothrow) int[num_elements];
    if (!data) {
        cerr << "Memory allocation failed!" << endl;
        return 1;
    }

    // Initialize the data sequentially
    cout << "Initializing data..." << endl;
    for (size_t i = 0; i < num_elements; ++i) {
        data[i] = i;
    }

    cout << "Data initialization complete." << endl;

    // Randomly read the data
    cout << "Randomly reading data..." << endl;

    size_t num_reads = 1000000; // Total number of random reads
    long long total_sum = 0;
    random_read(data, num_elements, num_reads, total_sum, static_cast<unsigned>(time(NULL)));

    cout << "Random reading complete. Total sum = " << total_sum << endl;

    delete[] data;

    return 0;
}
