#include <math.h>
#include <stdlib.h>
#include "rebxtools.h"

/*void rebxtools_inertial_to_jacobi_posvel(void){
	double s_x = particles[N_megno].m * particles[N_megno].x;
	double s_y = particles[N_megno].m * particles[N_megno].y;
	double s_z = particles[N_megno].m * particles[N_megno].z;
	double s_vx = particles[N_megno].m * particles[N_megno].vx;
	double s_vy = particles[N_megno].m * particles[N_megno].vy;
	double s_vz = particles[N_megno].m * particles[N_megno].vz;
	for (unsigned int i=1+N_megno;i<N;i++){
		const double ei = etai[i-1-N_megno];
		const struct particle pi = particles[i];
		const double pme = eta[i-N_megno]*ei;
		p_j[i].x = pi.x - s_x*ei;
		p_j[i].y = pi.y - s_y*ei;
		p_j[i].z = pi.z - s_z*ei;
		p_j[i].vx = pi.vx - s_vx*ei;
		p_j[i].vy = pi.vy - s_vy*ei;
		p_j[i].vz = pi.vz - s_vz*ei;
		s_x  = s_x  * pme + pi.m*p_j[i].x ;
		s_y  = s_y  * pme + pi.m*p_j[i].y ;
		s_z  = s_z  * pme + pi.m*p_j[i].z ;
		s_vx = s_vx * pme + pi.m*p_j[i].vx;
		s_vy = s_vy * pme + pi.m*p_j[i].vy;
		s_vz = s_vz * pme + pi.m*p_j[i].vz;
	}
	p_j[N_megno].x = s_x * Mtotali;
	p_j[N_megno].y = s_y * Mtotali;
	p_j[N_megno].z = s_z * Mtotali;
	p_j[N_megno].vx = s_vx * Mtotali;
	p_j[N_megno].vy = s_vy * Mtotali;
	p_j[N_megno].vz = s_vz * Mtotali;
}*/

struct reb_orbit rebxtools_orbit_nan(void){
	struct reb_orbit o;
	o.a = NAN;
	o.r = NAN;
	o.h = NAN;
	o.P = NAN;
	o.l = NAN;
	o.e = NAN;
	o.inc = NAN;
	o.Omega = NAN;
	o.omega = NAN;
	o.f = NAN;
	return o;
}

static const struct reb_orbit reb_orbit_nan = {.r = NAN, .v = NAN, .h = NAN, .P = NAN, .n = NAN, .a = NAN, .e = NAN, .inc = NAN, .Omega = NAN, .omega = NAN, .pomega = NAN, .f = NAN, .M = NAN, .l = NAN};

#define MIN_REL_ERROR 1.0e-12	///< Close to smallest relative floating point number, used for orbit calculation
#define TINY 1.E-308 		///< Close to smallest representable floating point number, used for orbit calculation
#define MIN_INC 1.e-8		///< Below this inclination, the broken angles pomega and theta equal the corresponding 
							///< unbroken angles to within machine precision, so a practical boundary for planar orbits
							//
// returns acos(num/denom), using disambiguator to tell which quadrant to return.  
// will return 0 or pi appropriately if num is larger than denom by machine precision
// and will return 0 if denom is exactly 0.

static double acos2(double num, double denom, double disambiguator){
	double val;
	double cosine = num/denom;
	if(cosine > -1. && cosine < 1.){
		val = acos(cosine);
		if(disambiguator < 0.){
			val = - val;
		}
	}
	else{
		val = (cosine <= -1.) ? M_PI : 0.;
	}
	return val;
}

void rebxtools_orbit2p(double G, struct reb_particle* p, struct reb_particle* primary, struct reb_orbit o){
	int* err = malloc(sizeof(int));
	struct reb_particle p2 = rebxtools_orbit_to_particle(G,*primary, p->m, o.a, o.e, o.inc, o.Omega, o.omega, o.f, err);
	p->x = p2.x;
	p->y = p2.y;
	p->z = p2.z;
	p->vx = p2.vx;
	p->vy = p2.vy;
	p->vz = p2.vz;
}

static const struct reb_particle reb_particle_nan = {.x = NAN, .y = NAN, .z = NAN, .vx = NAN, .vy = NAN, .vz = NAN, .ax = NAN, .ay = NAN, .az = NAN, .m = NAN, .r = NAN, .lastcollision = NAN, .c = 0, .id = NAN};

struct reb_particle rebxtools_orbit_to_particle(double G, struct reb_particle primary, double m, double a, double e, double inc, double Omega, double omega, double f, int* err){
	if(e == 1.){
		*err = 1; 		// Can't initialize a radial orbit with orbital elements.
		return reb_particle_nan;
	}
	if(e < 0.){
		*err = 2; 		// Eccentricity must be greater than or equal to zero.
		return reb_particle_nan;
	}
	if(e > 1.){
		if(a > 0.){
			*err = 3; 	// Bound orbit (a > 0) must have e < 1. 
			return reb_particle_nan;
		}
	}
	else{
		if(a < 0.){
			*err =4; 	// Unbound orbit (a < 0) must have e > 1.
			return reb_particle_nan;
		}
	}
	if(e*cos(f) < -1.){
		*err = 5;		// Unbound orbit can't have f set beyond the range allowed by the asymptotes set by the parabola.
		return reb_particle_nan;
	}

	struct reb_particle p = {0};
	p.m = m;
	double r = a*(1-e*e)/(1 + e*cos(f));
	double v0 = sqrt(G*(m+primary.m)/a/(1.-e*e)); // in this form it works for elliptical and hyperbolic orbits

	double cO = cos(Omega);
	double sO = sin(Omega);
	double co = cos(omega);
	double so = sin(omega);
	double cf = cos(f);
	double sf = sin(f);
	double ci = cos(inc);
	double si = sin(inc);
	
	// Murray & Dermott Eq 2.122
	p.x = primary.x + r*(cO*(co*cf-so*sf) - sO*(so*cf+co*sf)*ci);
	p.y = primary.y + r*(sO*(co*cf-so*sf) + cO*(so*cf+co*sf)*ci);
	p.z = primary.z + r*(so*cf+co*sf)*si;

	// Murray & Dermott Eq. 2.36 after applying the 3 rotation matrices from Sec. 2.8 to the velocities in the orbital plane
	p.vx = primary.vx + v0*((e+cf)*(-ci*co*sO - cO*so) - sf*(co*cO - ci*so*sO));
	p.vy = primary.vy + v0*((e+cf)*(ci*co*cO - sO*so)  - sf*(co*sO + ci*so*cO));
	p.vz = primary.vz + v0*((e+cf)*co*si - sf*si*so);
	
	p.ax = 0; 	p.ay = 0; 	p.az = 0;

	return p;
}

void rebxtools_move_to_com(struct reb_simulation* const r){
	const int N = r->N;
	struct reb_particle* restrict const particles = r->particles;
	struct reb_particle com = rebxtools_get_com(r);
	for(int i=0; i<N; i++){
		particles[i].x -= com.x;
		particles[i].y -= com.y;
		particles[i].z -= com.z;
		particles[i].vx -= com.vx;
		particles[i].vy -= com.vy;
		particles[i].vz -= com.vz;
	}
}

struct reb_particle rebxtools_get_com_of_pair(struct reb_particle p1, struct reb_particle p2){
	p1.x   = p1.x*p1.m + p2.x*p2.m;		
	p1.y   = p1.y*p1.m + p2.y*p2.m;
	p1.z   = p1.z*p1.m + p2.z*p2.m;
	p1.vx  = p1.vx*p1.m + p2.vx*p2.m;
	p1.vy  = p1.vy*p1.m + p2.vy*p2.m;
	p1.vz  = p1.vz*p1.m + p2.vz*p2.m;
	p1.m  += p2.m;
	if (p1.m>0.){
		p1.x  /= p1.m;
		p1.y  /= p1.m;
		p1.z  /= p1.m;
		p1.vx /= p1.m;
		p1.vy /= p1.m;
		p1.vz /= p1.m;
	}
	return p1;
}

struct reb_particle rebxtools_get_com(struct reb_simulation* const r){
	struct reb_particle com = {.m=0, .x=0, .y=0, .z=0, .vx=0, .vy=0, .vz=0};
	const int N = r->N;
	struct reb_particle* restrict const particles = r->particles;
	for (int i=0;i<N;i++){
		com = rebxtools_get_com_of_pair(com, particles[i]);
	}
	return com;
}

