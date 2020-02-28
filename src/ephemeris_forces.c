/** * @file central_force.c
 * @brief   A general central force.
 * @author  Dan Tamayo <tamayo.daniel@gmail.com>
 * 
 * @section     LICENSE
 * Copyright (c) 2015 Dan Tamayo, Hanno Rein
 *
 * This file is part of reboundx.
 *
 * reboundx is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * reboundx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The section after the dollar signs gets built into the documentation by a script.  All lines must start with space * space like below.
 * Tables always must be preceded and followed by a blank line.  See http://docutils.sourceforge.net/docs/user/rst/quickstart.html for a primer on rst.
 * $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
 *
 * $Central Force$       // Effect category (must be the first non-blank line after dollar signs and between dollar signs to be detected by script).
 *
 * ======================= ===============================================
 * Authors                 D. Tamayo
 * Implementation Paper    *In progress*
 * Based on                None
 * C Example               :ref:`c_example_central_force`
 * Python Example          `CentralForce.ipynb <https://github.com/dtamayo/reboundx/blob/master/ipython_examples/CentralForce.ipynb>`_.
 * ======================= ===============================================
 * 
 * Adds a general central acceleration of the form a=Acentral*r^gammacentral, outward along the direction from a central particle to the body.
 * Effect is turned on by adding Acentral and gammacentral parameters to a particle, which will act as the central body for the effect,
 * and will act on all other particles.
 *
 * **Effect Parameters**
 * 
 * None
 *
 * **Particle Parameters**
 *
 * ============================ =========== ==================================================================
 * Field (C type)               Required    Description
 * ============================ =========== ==================================================================
 * Acentral (double)             Yes         Normalization for central acceleration.
 * gammacentral (double)         Yes         Power index for central acceleration.
 * ============================ =========== ==================================================================
 * 
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
#include "rebound.h"
#include "reboundx.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "spk.h"

// these are the body codes for the user to specify
enum {
        PLAN_BAR,                       // <0,0,0>
        PLAN_SOL,                       // Sun (in barycentric)
        PLAN_EAR,                       // Earth centre
        PLAN_EMB,                       // Earth-Moon barycentre
        PLAN_LUN,                       // Moon centre
        PLAN_MER,                       // ... plus the rest
        PLAN_VEN,
        PLAN_MAR,
        PLAN_JUP,
        PLAN_SAT,
        PLAN_URA,
        PLAN_NEP,
        PLAN_PLU,	

        _NUM_TEST,
};

// these are array indices for the internal interface
enum {
        JPL_MER,                        // Mercury
        JPL_VEN,                        // Venus
        JPL_EMB,                        // Earth
        JPL_MAR,                        // Mars
        JPL_JUP,                        // Jupiter
        JPL_SAT,                        // Saturn
        JPL_URA,                        // Uranus
        JPL_NEP,                        // Neptune
        JPL_PLU,                        // Pluto
        JPL_LUN,                        // Moon (geocentric)
        JPL_SUN,                        // the Sun
        JPL_NUT,                        // nutations
        JPL_LIB,                        // lunar librations
        JPL_MAN,                        // lunar mantle
        JPL_TDB,                        // TT-TDB (< 2 ms)

        _NUM_JPL,
};

struct _jpl_s {
        double beg, end;                // begin and end times
        double inc;                     // time step size
        double cau;                     // definition of AU
        double cem;                     // Earth/Moon mass ratio
        int32_t num;                    // number of constants
        int32_t ver;                    // ephemeris version
        int32_t off[_NUM_JPL];          // indexing offset
        int32_t ncf[_NUM_JPL];          // number of chebyshev coefficients
        int32_t niv[_NUM_JPL];          // number of interpolation intervals
        int32_t ncm[_NUM_JPL];          // number of components / dimension
///
        size_t len, rec;                // file and record sizes
        void *map;                      // memory mapped location
};

// this stores the position+velocity
/*
struct mpos_s {
        double u[3];                    // position vector [AU]
        double v[3];                    // velocity vector [AU/day]
        double jde;                     // TDT time [days]
};
*/

struct _jpl_s * jpl_init(void);
int jpl_free(struct _jpl_s *jpl);
void jpl_work(double *P, int ncm, int ncf, int niv, double t0, double t1, double *u, double *v);
int jpl_calc(struct _jpl_s *jpl, struct mpos_s *now, double jde, int n, int m);

/////// private interface :


static inline void vecpos_off(double *u, const double *v, const double w)
        { u[0] += v[0] * w; u[1] += v[1] * w; u[2] += v[2] * w; }
static inline void vecpos_set(double *u, const double *v)
        { u[0] = v[0]; u[1] = v[1]; u[2] = v[2]; }
static inline void vecpos_nul(double *u)
        { u[0] = u[1] = u[2] = 0.0; }
static inline void vecpos_div(double *u, double v)
        { u[0] /= v; u[1] /= v; u[2] /= v; }

/*
 *  jpl_work
 *
 *  Interpolate the appropriate Chebyshev polynomial coefficients.
 *
 *      ncf - number of coefficients per component
 *      ncm - number of components (ie: 3 for most)
 *      niv - number of intervals / sets of coefficients
 *
 */

void jpl_work(double *P, int ncm, int ncf, int niv, double t0, double t1, double *u, double *v)
{
        double T[24], S[24];
        double t, c;
        int p, m, n, b;

        // adjust to correct interval
        t = t0 * (double)niv;
        t0 = 2.0 * fmod(t, 1.0) - 1.0;
        c = (double)(niv * 2) / t1 / 86400.0;
        b = (int)t;

        // set up Chebyshev polynomials and derivatives
        T[0] = 1.0; T[1] = t0;
        S[0] = 0.0; S[1] = 1.0;

        for (p = 2; p < ncf; p++) {
                T[p] = 2.0 * t0 * T[p-1] - T[p-2];
                S[p] = 2.0 * t0 * S[p-1] + 2.0 * T[p-1] - S[p-2];
        }

        // compute the position/velocity
        for (m = 0; m < ncm; m++) {
                u[m] = v[m] = 0.0;
                n = ncf * (m + b * ncm);

                for (p = 0; p < ncf; p++) {
                        u[m] += T[p] * P[n+p];
                        v[m] += S[p] * P[n+p] * c;
                }
        }
}
 
/*
 *  jpl_init
 *
 *  Initialise everything needed ... probaly not be compatible with a non-430 file.
 *
 */

struct _jpl_s * jpl_init(void)
{
        struct _jpl_s *jpl;
        struct stat sb;
        char buf[256];
        ssize_t ret;
        off_t off;
        int fd, p;

//      snprintf(buf, sizeof(buf), "/home/blah/wherever/linux_p1550p2650.430");
        snprintf(buf, sizeof(buf), "linux_p1550p2650.430");
	//snprintf(buf, sizeof(buf), "/Users/aryaakmal/Documents/REBOUND/rebound/reboundx/examples/ephem_forces/linux_p1550p2650.430");

        if ((fd = open(buf, O_RDONLY)) < 0)
                return NULL;

        jpl = malloc(sizeof(struct _jpl_s));
        memset(jpl, 0, sizeof(struct _jpl_s));

        if (fstat(fd, &sb) < 0)
                goto err;
        if (lseek(fd, 0x0A5C, SEEK_SET) < 0)
                goto err;

        // read header
        ret  = read(fd, &jpl->beg, sizeof(double));
        ret += read(fd, &jpl->end, sizeof(double));
        ret += read(fd, &jpl->inc, sizeof(double));
        ret += read(fd, &jpl->num, sizeof(int32_t));
        ret += read(fd, &jpl->cau, sizeof(double));
        ret += read(fd, &jpl->cem, sizeof(double));

        // number of coefficients is assumed
        for (p = 0; p < _NUM_JPL; p++)
                jpl->ncm[p] = 3;

        jpl->ncm[JPL_NUT] = 2;
        jpl->ncm[JPL_TDB] = 1;

        for (p = 0; p < 12; p++) {
                ret += read(fd, &jpl->off[p], sizeof(int32_t));
                ret += read(fd, &jpl->ncf[p], sizeof(int32_t));
                ret += read(fd, &jpl->niv[p], sizeof(int32_t));
        }

        ret += read(fd, &jpl->ver,     sizeof(int32_t));
        ret += read(fd, &jpl->off[12], sizeof(int32_t));
        ret += read(fd, &jpl->ncf[12], sizeof(int32_t));
        ret += read(fd, &jpl->niv[12], sizeof(int32_t));

        // skip the remaining constants
        off = 6 * (jpl->num - 400);

        if (lseek(fd, off, SEEK_CUR) < 0)
                goto err;

        // finishing reading
        for (p = 13; p < 15; p++) {
                ret += read(fd, &jpl->off[p], sizeof(int32_t));
                ret += read(fd, &jpl->ncf[p], sizeof(int32_t));
                ret += read(fd, &jpl->niv[p], sizeof(int32_t));
        }

        // adjust for correct indexing (ie: zero based)
        for (p = 0; p < _NUM_JPL; p++)
                jpl->off[p] -= 1;

        // save file size, and determine 'kernel size'
        jpl->len = sb.st_size;
        jpl->rec = sizeof(double) * 2;

        for (p = 0; p < _NUM_JPL; p++)
                jpl->rec += sizeof(double) * jpl->ncf[p] * jpl->niv[p] * jpl->ncm[p];

        // memory map the file, which makes us thread-safe with kernel caching
        jpl->map = mmap(NULL, jpl->len, PROT_READ, MAP_SHARED, fd, 0);

        if (jpl->map == NULL)
                goto err;

        // this file descriptor is no longer needed since we are memory mapped
        if (close(fd) < 0)
                { ; } // perror ...
        if (madvise(jpl->map, jpl->len, MADV_RANDOM) < 0)
                { ; } // perror ...

        return jpl;

err:    close(fd);
        free(jpl);

        return NULL;
}

/*
 *  jpl_free
 *
 */
int jpl_free(struct _jpl_s *jpl)
{
        if (jpl == NULL)
                return -1;

        if (munmap(jpl->map, jpl->len) < 0)
                { ; } // perror...

        memset(jpl, 0, sizeof(struct _jpl_s));
        free(jpl);
        return 0;
}
/*
 *  jpl_calc
 *
 *  Caculate the position+velocity in _equatorial_ coordinates.
 *
 */

static void _bar(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { vecpos_nul(pos->u); vecpos_nul(pos->v); }
static void _sun(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_SUN]], jpl->ncm[JPL_SUN], jpl->ncf[JPL_SUN], jpl->niv[JPL_SUN], t, jpl->inc, pos->u, pos->v); }
static void _emb(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_EMB]], jpl->ncm[JPL_EMB], jpl->ncf[JPL_EMB], jpl->niv[JPL_EMB], t, jpl->inc, pos->u, pos->v); }
static void _mer(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_MER]], jpl->ncm[JPL_MER], jpl->ncf[JPL_MER], jpl->niv[JPL_MER], t, jpl->inc, pos->u, pos->v); }
static void _ven(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_VEN]], jpl->ncm[JPL_VEN], jpl->ncf[JPL_VEN], jpl->niv[JPL_VEN], t, jpl->inc, pos->u, pos->v); }
static void _mar(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_MAR]], jpl->ncm[JPL_MAR], jpl->ncf[JPL_MAR], jpl->niv[JPL_MAR], t, jpl->inc, pos->u, pos->v); }
static void _jup(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_JUP]], jpl->ncm[JPL_JUP], jpl->ncf[JPL_JUP], jpl->niv[JPL_JUP], t, jpl->inc, pos->u, pos->v); }
static void _sat(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_SAT]], jpl->ncm[JPL_SAT], jpl->ncf[JPL_SAT], jpl->niv[JPL_SAT], t, jpl->inc, pos->u, pos->v); }
static void _ura(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_URA]], jpl->ncm[JPL_URA], jpl->ncf[JPL_URA], jpl->niv[JPL_URA], t, jpl->inc, pos->u, pos->v); }
static void _nep(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_NEP]], jpl->ncm[JPL_NEP], jpl->ncf[JPL_NEP], jpl->niv[JPL_NEP], t, jpl->inc, pos->u, pos->v); }
static void _plu(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
        { jpl_work(&z[jpl->off[JPL_PLU]], jpl->ncm[JPL_PLU], jpl->ncf[JPL_PLU], jpl->niv[JPL_PLU], t, jpl->inc, pos->u, pos->v); }

static void _ear(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
{
        struct mpos_s emb, lun;

        jpl_work(&z[jpl->off[JPL_EMB]], jpl->ncm[JPL_EMB], jpl->ncf[JPL_EMB], jpl->niv[JPL_EMB], t, jpl->inc, emb.u, emb.v);
        jpl_work(&z[jpl->off[JPL_LUN]], jpl->ncm[JPL_LUN], jpl->ncf[JPL_LUN], jpl->niv[JPL_LUN], t, jpl->inc, lun.u, lun.v);

        vecpos_set(pos->u, emb.u);
        vecpos_off(pos->u, lun.u, -1.0 / (1.0 + jpl->cem));

        vecpos_set(pos->v, emb.v);
        vecpos_off(pos->v, lun.v, -1.0 / (1.0 + jpl->cem));
}

/* This was not fully tested */
static void _lun(struct _jpl_s *jpl, double *z, double t, struct mpos_s *pos)
{
        struct mpos_s emb, lun;

        jpl_work(&z[jpl->off[JPL_EMB]], jpl->ncm[JPL_EMB], jpl->ncf[JPL_EMB], jpl->niv[JPL_EMB], t, jpl->inc, emb.u, emb.v);
        jpl_work(&z[jpl->off[JPL_LUN]], jpl->ncm[JPL_LUN], jpl->ncf[JPL_LUN], jpl->niv[JPL_LUN], t, jpl->inc, lun.u, lun.v);

        vecpos_set(pos->u, emb.u);
        vecpos_off(pos->u, lun.u, jpl->cem / (1.0 + jpl->cem));

        vecpos_set(pos->v, emb.v);
        vecpos_off(pos->v, lun.v, jpl->cem / (1.0 + jpl->cem));
}


// function pointers are used to avoid a pointless switch statement
// Added _lun here (2020 Feb 26)
static void (* _help[_NUM_TEST])(struct _jpl_s *, double *, double, struct mpos_s *)
    = { _bar, _sun, _ear, _emb, _lun, _mer, _ven, _mar, _jup, _sat, _ura, _nep, _plu};

int jpl_calc(struct _jpl_s *pl, struct mpos_s *now, double jde, int n, int m)
{
        struct mpos_s pos;
        struct mpos_s ref;
        double t, *z;
        u_int32_t blk;
        int p;

        if (pl == NULL || now == NULL)
                return -1;

        // check if covered by this file
        if (jde < pl->beg || jde > pl->end || pl->map == NULL)
                return -1;

        // compute record number and 'offset' into record
        blk = (u_int32_t)((jde - pl->beg) / pl->inc);
        t = fmod(jde - pl->beg, pl->inc) / pl->inc;
        z = pl->map + (blk + 2) * pl->rec;

        // the magick of function pointers
        _help[n](pl, z, t, &pos);
        _help[m](pl, z, t, &ref);

        for (p = 0; p < 3; p++) {
                now->u[p] = pos.u[p] - ref.u[p];
                now->v[p] = pos.v[p] - ref.v[p];
        }

        now->jde = jde;
        return 0;
}

// Added gravitational constant G for the GR stuff (2020 Feb 26)
// Added vx, vy, vz (2020 Feb 27)
void ephem(const double G, const int i, const double t, double* const m,
		  double* const x, double* const y, double* const z,
		  double* const vx, double* const vy, double* const vz){
    const double n = 1.;
    const double mu = 1.e-3;
    const double m0 = 1.-mu;
    const double m1 = mu;

    static int initialized = 0;

    static struct _jpl_s *pl;
    static struct spk_s *spl;
    struct mpos_s now;
    double jde;

    //printf("G: %lf\n", G);
    double M[11] =
      {
	0.295912208285591100E-03, // 0  sun  
	0.491248045036476000E-10, // 1  mercury
	0.724345233264412000E-09, // 2  venus
	0.888769244512563400E-09, // 3  earth
	0.109318945074237400E-10, // 4  moon
	0.954954869555077000E-10, // 5  mars
	0.282534584083387000E-06, // 6  jupiter
	0.845970607324503000E-07, // 7  saturn
	0.129202482578296000E-07, // 8  uranus
	0.152435734788511000E-07, // 9  neptune
	0.217844105197418000E-11, // 10 pluto
      };

    for(int k=0; k<11; k++){
      M[k] /= G;
    }

    if (initialized == 0){
      
      if ((pl = jpl_init()) == NULL) {
	fprintf(stderr, "could not load DE430 file, fool!\n");
	exit(EXIT_FAILURE);
      }

      if ((spl = spk_init("sb431-n16s.bsp")) == NULL) {
	fprintf(stderr, "could not load sb431-n16 file, fool!\n");
	exit(EXIT_FAILURE);
      }
      printf("initialization complete\n");

      initialized = 1;

    }

    //jde = t + 2450123.7;  // t=0 is Julian day 2450123.7
    jde = t;  

    if (i==0){                           
      *m = M[0];
      jpl_calc(pl, &now, jde, PLAN_SOL, PLAN_BAR); //sun in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==1){
      *m = M[1];
      jpl_calc(pl, &now, jde, PLAN_MER, PLAN_BAR); //mercury in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==2){
      *m = M[2];
      jpl_calc(pl, &now, jde, PLAN_VEN, PLAN_BAR); //venus in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==3){
      *m = M[3];
      jpl_calc(pl, &now, jde, PLAN_EAR, PLAN_BAR); //earth in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==4){
      *m = M[4];
      jpl_calc(pl, &now, jde, PLAN_LUN, PLAN_BAR); //moon in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }
    
    if (i==5){
      *m = M[5];
      jpl_calc(pl, &now, jde, PLAN_MAR, PLAN_BAR); //mars in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==6){
      *m = M[6];
      jpl_calc(pl, &now, jde, PLAN_JUP, PLAN_BAR); //jupiter in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==7){
      *m = M[7];
      jpl_calc(pl, &now, jde, PLAN_SAT, PLAN_BAR); //saturn in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }
    
    if (i==8){
      *m = M[8];
      jpl_calc(pl, &now, jde, PLAN_URA, PLAN_BAR); //uranus in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }
    
    if (i==9){
      *m = M[9];
      jpl_calc(pl, &now, jde, PLAN_NEP, PLAN_BAR); //neptune in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }

    if (i==10){
      *m = M[10];
      jpl_calc(pl, &now, jde, PLAN_PLU, PLAN_BAR); //neptune in barycentric coords. 
      vecpos_div(now.u, pl->cau);
      vecpos_div(now.v, (pl->cau/86400.));
      *x = now.u[0];
      *y = now.u[1];
      *z = now.u[2];
      *vx = now.v[0];
      *vy = now.v[1];
      *vz = now.v[2];
    }
    
}

static void ast_ephem(const double G, const int i, const double t, double* const m, double* const x, double* const y, double* const z){
    const double n = 1.;
    const double mu = 1.e-3;
    const double m0 = 1.-mu;
    const double m1 = mu;

    static int initialized = 0;

    static struct _jpl_s *pl;
    static struct spk_s *spl;
    struct mpos_s pos;    
    double jde;

    if(i<0 || i>15){
      fprintf(stderr, "asteroid out of range\n");
      exit(EXIT_FAILURE);
    }

    // 1 Ceres, 4 Vesta, 2 Pallas, 10 Hygiea, 31 Euphrosyne, 704 Interamnia,
    // 511 Davida, 15 Eunomia, 3 Juno, 16 Psyche, 65 Cybele, 88 Thisbe, 
    // 48 Doris, 52 Europa, 451 Patientia, 87 Sylvia
    
    double M[16] =
      {
	1.400476556172344e-13, // ceres
	3.854750187808810e-14, // vesta
	3.104448198938713e-14, // pallas
	1.235800787294125e-14, // hygiea
	6.343280473648602e-15, // euphrosyne
	5.256168678493662e-15, // interamnia
	5.198126979457498e-15, // davida
	4.678307418350905e-15, // eunomia
	3.617538317147937e-15, // juno
	3.411586826193812e-15, // psyche
	3.180659282652541e-15, // cybele
	2.577114127311047e-15, // thisbe
	2.531091726015068e-15, // doris
	2.476788101255867e-15, // europa
	2.295559390637462e-15, // patientia
	2.199295173574073e-15, // sylvia
      };

    for(int k=0; k<16; k++){
      M[k] /= G;
    }

    if (initialized == 0){
      
      if ((spl = spk_init("sb431-n16s.bsp")) == NULL) {
	fprintf(stderr, "could not load sb431-n16 file, fool!\n");
	exit(EXIT_FAILURE);
      }
      printf("asteroid initialization complete\n");

      initialized = 1;

    }

    jde = t;

    *m = M[i];
    spk_calc(spl, i, jde, &pos);          
    //vecpos_div(pos.u, spl->cau);
    *x = pos.u[0];
    *y = pos.u[1];
    *z = pos.u[2];
    
}

void rebx_ephemeris_forces(struct reb_simulation* const sim, struct rebx_force* const force, struct reb_particle* const particles, const int N){
    const int* const N_ephem = rebx_get_param(sim->extras, force->ap, "N_ephem");
    const int* const N_ast = rebx_get_param(sim->extras, force->ap, "N_ast");
    if (N_ephem == NULL){
        fprintf(stderr, "REBOUNDx Error: Need to set N_ephem for ephemeris_forces\n");
        return;
    }

    const double G = sim->G;
    const double t = sim->t;

    double* c = rebx_get_param(sim->extras, force->ap, "c");
    if (c == NULL){
        reb_error(sim, "REBOUNDx Error: Need to set speed of light in gr effect.  See examples in documentation.\n");
        return;
    }
    const double C2 = (*c)*(*c);

    double m, x, y, z, vx, vy, vz;
    double xs, ys, zs, vxs, vys, vzs;
    double xe, ye, ze, vxe, vye, vze;    

    ephem(G, 3, t, &m, &xe, &ye, &ze, &vxe, &vye, &vze); // Get position and mass of earth

    // Calculate acceleration due to sun and planets
    for (int i=0; i<*N_ephem; i++){
        ephem(G, i, t, &m, &x, &y, &z, &vx, &vy, &vz); // Get position and mass of massive body i.
        for (int j=0; j<N; j++){
  	  // Compute position vector of test particle j relative to massive body i.
            const double dx = particles[j].x - x; 
            const double dy = particles[j].y - y;
            const double dz = particles[j].z - z;
            const double _r = sqrt(dx*dx + dy*dy + dz*dz);
            const double prefac = G*m/(_r*_r*_r);
            particles[j].ax -= prefac*dx;
            particles[j].ay -= prefac*dy;
            particles[j].az -= prefac*dz;
        }
    }

    // Get position, velocity, and mass of the sun wrt barycenter
    // in order to translate heliocentric asteroids to barycenter.
    ephem(G, 0, t, &m, &xs, &ys, &zs, &vxs, &vys, &vzs); 

    // Now calculate acceleration due to massive asteroids
    for (int i=0; i<*N_ast; i++){
        ast_ephem(G, i, t, &m, &x, &y, &z); // Get position and mass of asteroid i.
	x += xs;
	y += ys;
	z += zs;
        for (int j=0; j<N; j++){
  	  // Compute position vector of test particle j relative to massive body i.
            const double dx = particles[j].x - x; 
            const double dy = particles[j].y - y;
            const double dz = particles[j].z - z;
            const double _r = sqrt(dx*dx + dy*dy + dz*dz);
            const double prefac = G*m/(_r*_r*_r);
            particles[j].ax -= prefac*dx;
            particles[j].ay -= prefac*dy;
            particles[j].az -= prefac*dz;
        }
    }


    // Here is the GR treatment
    const double Msun = 1.0; // mass of sun in solar masses.
    const double mu = G*Msun; // careful here.  We are assuming that the central body is at the barycenter.
    const int max_iterations = 10; // careful of hard-coded parameter.
    for (int i=0; i<N; i++){
        struct reb_particle p = particles[i];
        struct reb_vec3d vi;
        vi.x = p.vx;
        vi.y = p.vy;
        vi.z = p.vz;
        double vi2=vi.x*vi.x + vi.y*vi.y + vi.z*vi.z;
        const double ri = sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
        int q = 0;
        double A = (0.5*vi2 + 3.*mu/ri)/C2;
        struct reb_vec3d old_v;
        for(q=0; q<max_iterations; q++){
            old_v.x = vi.x;
            old_v.y = vi.y;
            old_v.z = vi.z;
            vi.x = p.vx/(1.-A);
            vi.y = p.vy/(1.-A);
            vi.z = p.vz/(1.-A);
            vi2 =vi.x*vi.x + vi.y*vi.y + vi.z*vi.z;
            A = (0.5*vi2 + 3.*mu/ri)/C2;
            const double dvx = vi.x - old_v.x;
            const double dvy = vi.y - old_v.y;
            const double dvz = vi.z - old_v.z;
            if ((dvx*dvx + dvy*dvy + dvz*dvz)/vi2 < DBL_EPSILON*DBL_EPSILON){
                break;
            }
        }
        const int default_max_iterations = 10;
        if(q==default_max_iterations){
            reb_warning(sim, "REBOUNDx Warning: 10 iterations in gr.c failed to converge. This is typically because the perturbation is too strong for the current implementation.");
        }
  
        const double B = (mu/ri - 1.5*vi2)*mu/(ri*ri*ri)/C2;
        const double rdotrdot = p.x*p.vx + p.y*p.vy + p.z*p.vz;
        
        struct reb_vec3d vidot;
        vidot.x = p.ax + B*p.x;
        vidot.y = p.ay + B*p.y;
        vidot.z = p.az + B*p.z;
        
        const double vdotvdot = vi.x*vidot.x + vi.y*vidot.y + vi.z*vidot.z;
        const double D = (vdotvdot - 3.*mu/(ri*ri*ri)*rdotrdot)/C2;

	//printf("gr: %le %le %le\n", B*(1.-A)*p.x - A*p.ax - D*vi.x, particles[i].ax, sqrt(C2));
	
        particles[i].ax += B*(1.-A)*p.x - A*p.ax - D*vi.x;
        particles[i].ay += B*(1.-A)*p.y - A*p.ay - D*vi.y;
        particles[i].az += B*(1.-A)*p.z - A*p.az - D*vi.z;

    }

    /*
    double dt = 1e-5;

    double vxm, vym, vzm;
    double vxp, vyp, vzp;
    
    ephem(G, 3, t+dt, &m, &x, &y, &z, &vxp, &vyp, &vzp); // Get position and velocity of earth.
    ephem(G, 3, t-dt, &m, &x, &y, &z, &vxm, &vym, &vzm); // Get position and velocity of earth.
    //printf("%le %le %le\n", vxp, vyp, vzp);

    double axe = (vxp-vxm)/(2.0*dt);
    double aye = (vyp-vym)/(2.0*dt);
    double aze = (vzp-vzm)/(2.0*dt);

    //printf("%lf %le %le %le\n", t, axe, aye, aze);
    //printf("%lf %le %le %le\n", t, particles[0].ax, particles[0].ay, particles[0].az);

    for (int i=0; i<N; i++){    

      particles[i].ax -= axe;
      particles[i].ay -= aye;
      particles[i].az -= aze;

    }
    */
    
    
}
