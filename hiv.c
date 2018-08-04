#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "project.h"
#include "mpi.h"

/*
 *  TODO: parallel
 *  TODO: better memory management (i.e convert petri dish to 1D array)
 */
MPI_Datatype mpiPixel;

int main(int argc, char** argv)
{

    //Initialize MPI environment
    MPI_Init(&argc, &argv);

    struct Pixel* petriDisheeetetr;


    //Initialize MPI variables
    int rank, numranks;
    MPI_Comm_size(MPI_COMM_WORLD, &numranks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); 

    //Create a datatype for Pixel
    MPI_Type_create_resized(MPI_INT, 0, sizeof(struct Pixel), &mpiPixel);
    MPI_Type_commit(&mpiPixel);

    // setting a seed for the random number generator
    srand(time(NULL));

    int gen = atoi(argv[1]);
    int size = atoi(argv[2]);

    // allocate (dynamically) memory to the petriDish, and init all memory locations to be whitespace
    struct Pixel* petriDish;
    struct Pixel* checkBuffer;

    if(rank == 0) {

	petriDish = allocatePetriDish(size);
	checkBuffer = allocatePetriDish(size);

	// populate the petri dish with cells or viruses randomly
	populatePetriDish(petriDish, size);
	populateBuffer(checkBuffer, size);

    } //end if rank == 0

    // incubatePetriDish
    incubatePetriDish(petriDish, checkBuffer, size, gen, rank, numranks);

    //Finalize MPI
    MPI_Finalize();

    return 0;
}

struct Pixel* allocatePetriDish(int size)
{
    struct Pixel* petriDish = malloc(size * size * sizeof(struct Pixel));

    return petriDish;
}

void populateBuffer(struct Pixel* buffer, int size)
{
    struct Pixel pixel = {255, 255, 255};

    for(int i = 0; i < size; i++)
    {
        for(int j = 0; j < size; j++)
        {
            buffer[i*size+j] = pixel;
        }
    }
}

void populatePetriDish(struct Pixel* petriDish, int size)
{
    for(int i = 0; i < size; i++)
    {
        for(int j = 0; j < size; j++)
        {
            // assume virus and cell both have 1% of spawning respectively, and 98% of empty area
            int randomPixel = rand() % 100;

            // default value for pixel (white)
            struct Pixel pixel = {255, 255, 255};

            // first, check if it is the border or not - if so, ignore
            if(isNotBorder(i, j, size))
            {
                // 98 means it's a virus (red)
                if(randomPixel == 98)
                {
                    pixel = (struct Pixel) {255, 0, 0};
                }
                // 99 means it's a cell (green)
                else if(randomPixel == 99)
                {
                    pixel = (struct Pixel) {0, 255, 0};
                }
            }

            petriDish[i*size+j] = pixel;
        }
    }
}

void incubatePetriDish(struct Pixel* petriDish, struct Pixel* checkBuffer, int size, int gen, int rank, int numranks)
{
    // Gen 0 Print
    petriDishToPPM(petriDish, size, 0);

    int bufSize = size*size/numranks;

    struct Pixel* littlePetri;
    struct Pixel* newPetri;

    //Create buffer for scatter
    littlePetri = allocatePetriDish(bufSize);
    newPetri = allocatePetriDish(bufSize);

    //Scatter petri dish to little petri buffers
    MPI_Scatter(petriDish, bufSize, mpiPixel, littlePetri, bufSize, mpiPixel, 0, MPI_COMM_WORLD);

    //Halo Exchange - need to move neighbor checking in halo exchange
    if(rank == 0) { //bufSize-1 / bufsize
	MPI_Sendrecv(littlePetri,bufSize,mpiPixel,1,0,newPetri,bufSize,mpiPixel,0,rank+1,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    }
    else { //1 / bufSize+1
	MPI_Sendrecv(littlePetri,bufSize,mpiPixel,rank+1,rank,littlePetri,bufSize,mpiPixel,rank,rank-1,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    }

    //Gather to new all little petri dishes
    MPI_Gather(littlePetri, bufSize, mpiPixel, newPetri, bufSize, mpiPixel, 0, MPI_COMM_WORLD);
    struct Pixel centerPixel;

    // iterates from 1 to gen
    for(int i = 1; i <= gen; i++)
    {
        populateBuffer(checkBuffer, size);

        // for this experiment, the edges are ignored and used as borders
        for(int x = 1; x < size - 1; x++)
        {
            for(int y = 1; y < size - 1; y++)
            {
                // set the center pixel using petri dish
                centerPixel = petriDish[x*size+y];

                // only check if centerPixel isn't free
                if(!isFree(centerPixel))
                {
                    // then give the center pixel to buffer
                    checkBuffer[x*size+y] = centerPixel;

                    // check the neighbors of the center pixel for infection
                    checkNeighbors(petriDish, checkBuffer, centerPixel, x, y, size);

                    // move pixel
                    movePixel(checkBuffer, size, x, y);
                } 
            }
        }

        // switch pointers between petriDish and checkBuffer and moveBuffer
        struct Pixel* temp = petriDish;
        petriDish = checkBuffer;
        checkBuffer = temp;

        // print petri dish to ppm after each gen
        petriDishToPPM(petriDish, size, i);
    }
}

void checkNeighbors(struct Pixel* petriDish, struct Pixel* checkBuffer, struct Pixel pixel, int x, int y, int size)
{
    struct Pixel neighbor;
    struct Pixel newPixel;

    for(int i = -1; i <= 1; i++)
    {
        for(int j = -1; j <= 1; j++)
        {
            //neighbor = petriDish[x + i][y + i];
            neighbor = petriDish[(x+i)*size+(y+i)];
            // cell free infection (40% chance)
            if(pixel.red == 0 && pixel.green == 255 && pixel.blue == 0 && neighbor.red == 255 && neighbor.green == 0 && neighbor.blue == 0)
            {
                int chanceOfInfection = rand() % 100;

                // (40% chance)
                if(chanceOfInfection >= 40)
                {
                    printf("cell free\n");

                    // blue if it's a cell to virus infection
                    newPixel = (struct Pixel) {0, 0, 255};

                    checkBuffer[x*size+y] = newPixel;
                }
            }
            // cell to cell infection (60% chance) 
            else if(pixel.red == 0 && pixel.green == 255 && pixel.blue == 0 && neighbor.red == 0 && neighbor.green == 0 && neighbor.blue == 255)
            {
                int chanceOfInfection = rand() % 100;

                // 60% chance
                if(chanceOfInfection >= 60)
                {
                    printf("cell to cell\n");

                    // magneta if it's a cell to cell infection
                    newPixel = (struct Pixel) {255, 0, 255};

                    checkBuffer[x*size+y] = newPixel;
                }
            }
        }
    }
}

void movePixel(struct Pixel* checkBuffer, int size, int x, int y)
{
    // generates from -1 ... 1, used for picking a random position for the cell to move to
    int i = rand() % 3 - 1;
    int j = rand() % 3 - 1;

    // check if the position the cell is moving to is the border
    if(isNotBorder(x + i, y + j, size) && i != 0 && j != 0)
    {
        struct Pixel neighbor = checkBuffer[(x+i)*size+(y+i)];

        // then check if the neighbor is free
        if(isFree(neighbor))
        {
            struct Pixel emptyPixel = (struct Pixel) {255, 255, 255};

            checkBuffer[(x+i)*size+(y+i)] = checkBuffer[x*size+y];
            checkBuffer[x*size+y] = emptyPixel;
        }
    }
}

int isFree(struct Pixel pixel)
{
    if(pixel.red == 255 && pixel.green == 255 && pixel.blue == 255)
    {
        return 1;
    }

    return 0;
}

int isNotBorder(int i, int j, int size)
{
    if (i != 0 && i != size - 1 && j != 0 && j != size - 1)
    {
        return 1;   
    }
 
    return 0;
}

void petriDishToPPM(struct Pixel* petriDish, int size, int gen)
{
    // TODO: make sure that each node/processor prints to a different ppm, this can be done by printf to a file based on the node's rank
    // format: P3 = rgb color in ASCII, width and height in pixels, 255 is the max value of each color

    // setting up filename, which will be in the directory results
    FILE* file;
    char* filename = malloc(45 * sizeof(char));
    sprintf(filename, "results/gen_%d_test.ppm", gen);

    // creates file or clears existing file
    file = fopen(filename, "w");

    fprintf(file, "P3\n%d %d\n255\n", size, size);     

    for(int i = 0; i < size; i++)
    {
        for(int j = 0; j < size; j++)
        {
            fprintf(file, "%d %d %d ", petriDish[i*size+j].red, petriDish[i*size+j].green, petriDish[i*size+j].blue);
        }

        fprintf(file, "\n");
    }

    fclose(file);
}

void printPetriDish(struct Pixel* petriDish, int size)
{
    for(int i = 0; i < size; i++)
    {   
        for(int j = 0; j < size; j++)
        {
            printf("%d %d %d ", petriDish[i*size+j].red, petriDish[i*size+j].green, petriDish[i*size+j].blue);
        }

        printf("\n");
    }
}

