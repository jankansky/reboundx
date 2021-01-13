#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "rebound.h"
#include "reboundx.h"

typedef struct {
  double t, x, y, z, vx, vy, vz, ax, ay, az;
} tstate;

typedef struct {
    double* t;
    double* state;
    int n_out;
    int n_particles;
} timestate;

void read_inputs(char *filename, double* tepoch, double* tstart, double* tstep, double* trange,
		 int *geocentric,
		 double **instate,
		 double **cov_mat,
		 int *n_particles);

int main(int argc, char* argv[]){

    //timestate ts;
    //timestate* tsp = (timestate*)malloc(sizeof(timestate));

    double *instate;    
    double* outstate = NULL;
    double* outtime = NULL;
    double* cov_mat;

    int n_alloc = 1000;
    int N = 10;
    outstate = (double *) malloc((8*n_alloc+1)*N*6*sizeof(double));
    outtime  = (double *) malloc((8*n_alloc+1)*sizeof(double));
    
    //outstate = (double *)malloc(n_alloc*8*N*6*sizeof(double));
    //outtime  = (double *)malloc(n_alloc*8*sizeof(double));

    //tsp->t = outtime;
    //tsp->state = outstate;
    //tsp->n_out = n_alloc;    

    int n_out;

    // Read ICs & integration params from file
    double tepoch, tstart, tstep, trange;
    int geocentric;
    int n_particles;

    if(argc >=2){
	read_inputs(argv[1], &tepoch, &tstart, &tstep, &trange, &geocentric, &instate, &cov_mat, &n_particles);
    }else{
//	read_inputs("initial_conditions.txt", &tepoch, &tstart, &tstep, &trange, &geocentric, &instate, &n_particles);
        printf("No Input File\n");
        exit(EXIT_FAILURE);
    }

    double scale;

    sscanf(argv[2], "%lf", &scale);

    int integration_function(double tstart, double tstep, double trange,
			     int geocentric,
			     int n_particles,
			     double* instate,
			     int n_alloc,			     
			     int *n_out,
			     double* outtime,
			     double* outstate);

    /*
    int integration_function(double tstart, double tstep, double trange,
			     int geocentric,
			     int n_particles,
			     double* instate,
			     timestate *ts);
    */

    // clearing out the file
    FILE* g = fopen("out_states.txt","w");

    //tsp->t = NULL;

    if(tstart >= tepoch){
	int status;

	status = integration_function(tepoch, tstep, trange+tstart-tepoch,
				      geocentric,
				      n_particles,
				      instate,             //ICs in instate correpond to tepoch
				      n_alloc,
				      &n_out,
				      outtime,
				      outstate);

	printf("n_out: %d %d\n", n_out, status);

	for(int i=0; i<(8*n_out+1); i++){
	    int offset = i*7*n_particles*6; //XYZ
	    //for(int j=0; j<1; j++){ 
	    for(int j=0; j<7*n_particles; j++){ //XYZ - hard coded "7" 6 var. particles per real particle
		fprintf(g,"%lf ", outtime[i]);
		fprintf(g,"%3d ", j);
		for(int k=0; k<6; k++){
		    fprintf(g,"%28.16e ", outstate[offset+6*j+k]);
		}
		fprintf(g,"\n");
	 }
     }
     
    }else{

	int status = integration_function(tepoch, -tstep, tstart-tepoch,
					  geocentric,
					  n_particles,
					  instate,            //IC for tepoch!
					  n_alloc,					  
					  &n_out,
					  outtime,
					  outstate);

	//n_out = tsp->n_out;
	//outtime = tsp->t;
	//outstate = tsp->state;
	
	for(int i=n_out-1; i>0; i--){
	    for(int j=0; j<7*n_particles; j++){ //XYZ
		fprintf(g,"%lf ", outtime[i]);
		fprintf(g,"%d ", j);
		int offset = (i*7*n_particles+j)*6; //XYZ
		for(int k=0; k<6; k++){
		    fprintf(g,"%28.16e ", outstate[offset+k]);
		}
		fprintf(g,"\n");
	    }
	}

	status = integration_function(tepoch, tstep, trange+tstart-tepoch,
				      geocentric,
				      n_particles,
				      instate,
				      n_alloc,					  				      
				      &n_out,
				      outtime,
				      outstate);


	//n_out = tsp->n_out;
	//outtime = tsp->t;
	//outstate = tsp->state;

	for(int i=0; i<n_out; i++){
	    for(int j=0; j<7*n_particles; j++){ //XYZ
		fprintf(g,"%lf ", outtime[i]);
		fprintf(g,"%d ", j);
		int offset = (i*7*n_particles+j)*6; //XYZ
		for(int k=0; k<6; k++){
		    fprintf(g,"%28.16e ", outstate[offset+k]);
		}
		fprintf(g,"\n");
	    }
	}
	
    }


    fclose(g);    

}

void read_inputs(char *filename, double* tepoch, double* tstart, double* tstep, double* trange,
		 int* geocentric, 
		 double **instate,
		 double **cov_mat,
		 int* n_particles){

     char label[100]; /* hardwired for length */
     FILE* fp;

     int np = 0;

     int n_allocated = 1;
     double* state = malloc(n_allocated*6*sizeof(double)); // Clean this up.

     double* cov = malloc(36*sizeof(double));
     
     if((fp = fopen(filename, "r")) != NULL){

      while(fscanf(fp, "%s", label) != EOF){
        if(!strcmp(label, "tepoch")){
	  fscanf(fp, "%lf", tepoch);     
        } else if(!strcmp(label, "tstart")){
          fscanf(fp, "%lf", tstart);
        } else if(!strcmp(label, "tstep")){
	  fscanf(fp, "%lf", tstep);
        } else if(!strcmp(label, "trange")){
	  fscanf(fp, "%lf", trange);
        } else if(!strcmp(label, "geocentric")){
         fscanf(fp, "%d", geocentric);
        } else if(!strcmp(label, "state")){
         fscanf(fp, "%lf%lf%lf", &state[6*np+0], &state[6*np+1], &state[6*np+2]);
         fscanf(fp, "%lf%lf%lf", &state[6*np+3], &state[6*np+4], &state[6*np+5]);	 
         np++;

	 // Resize the array, if needed.
	 if(np==n_allocated){
	     n_allocated *= 2;
	     state = realloc(state, n_allocated*6*sizeof(double));
	 }
	 
        } else if(!strcmp(label, "covariance")){
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[0], &cov[1], &cov[2],&cov[3], &cov[4], &cov[5]);
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[6], &cov[7], &cov[8],&cov[9], &cov[10], &cov[11]);
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[12], &cov[13], &cov[14],&cov[15], &cov[16], &cov[17]);
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[18], &cov[19], &cov[20],&cov[21], &cov[22], &cov[23]);
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[24], &cov[25], &cov[26],&cov[27], &cov[28], &cov[29]);
         fscanf(fp, "%lf%lf%lf%lf%lf%lf", &cov[30], &cov[31], &cov[32],&cov[33], &cov[34], &cov[35]);
 
        } else {
         printf("No label: %s\n", label);
         exit(EXIT_FAILURE);
        }
      }

      //deallocate unused space
      state = realloc(state, np*6*sizeof(double));

      *n_particles = np;
      *instate = state;      
      *cov_mat = cov;

      fclose(fp);

     }else{
       exit(EXIT_FAILURE);
     }

     return;
}

