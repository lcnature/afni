#include "mrilib.h"

#undef  GOOD
#define GOOD(i) (mask==NULL || mask[i])

static GA_setup *stup = NULL ;

/*---------------------------------------------------------------------------*/
static int gcd( int m , int n )
{
  while( m > 0 ){
    if( n > m ){ int t=m; m=n; n=t; } /* swap */
    m -= n ;
  }
  return n ;
}
static int find_relprime_fixed( int n )
{
   int dj , n5=n/5 ;
   if( n5 < 2 ) return 1 ;
   for( dj=n5 ; gcd(n,dj) > 1 ; dj++ ) ; /*nada*/
   return dj ;
}
#if 0
static int find_relprime_random( int n )
{
   int dj , n5=n/5 , n2=n/2 ;
   if( n5 < 2 ) return 1 ;
   do{ dj = n5 + lrand48()%n2 ; } while( gcd(n,dj) > 1 ) ;
   return dj ;
}
#endif
/*---------------------------------------------------------------------------*/

static MRI_IMAGE * GA_smooth( MRI_IMAGE *im , int meth , float rad )
{
   MRI_IMAGE *om=NULL ;

   ENTRY("GA_smooth") ;

   if( im == NULL || im->kind != MRI_float || rad <= 0.0f ) RETURN(NULL) ;

   switch( meth ){
     default:
     case GA_SMOOTH_GAUSSIAN:
       om = mri_to_float(im) ;
       FIR_blur_volume_3d( om->nx , om->ny , om->nz ,
                           1.0f   , 1.0f   , 1.0f   ,
                           MRI_FLOAT_PTR(om) ,
                           rad , rad , rad           ) ;
     break ;

     case GA_SMOOTH_MEDIAN:
       if( rad < 1.01f ) rad = 1.01f ;
       om = mri_medianfilter( im , rad , NULL , 0 ) ;
     break ;
   }

   RETURN(om) ;
}

/*---------------------------------------------------------------------------*/

#undef  FAR
#define FAR(i,j,k) far[(i)+(j)*nx+(k)*nxy]

#undef  CLIP
#define CLIP(mm,nn) if(mm < 0)mm=0; else if(mm > nn)mm=nn

static void GA_interp_NN( MRI_IMAGE *fim ,
                          int npp, float *ip, float *jp, float *kp, float *vv )
{
   int nx=fim->nx , ny=fim->ny , nz=fim->nz , nxy=nx*ny , ii,jj,kk , pp ;
   float nxh=nx-0.501f , nyh=ny-0.501f , nzh=nz-0.501f , xx,yy,zz ;
   float *far = MRI_FLOAT_PTR(fim) ;

   for( pp=0 ; pp < npp ; pp++ ){
     xx = ip[pp] ; if( xx < -0.499f || xx > nxh ){ vv[pp]=0.0f; continue; }
     yy = jp[pp] ; if( yy < -0.499f || yy > nyh ){ vv[pp]=0.0f; continue; }
     zz = kp[pp] ; if( zz < -0.499f || zz > nzh ){ vv[pp]=0.0f; continue; }

     ii = (int)(xx+0.5f) ; jj = (int)(yy+0.5f) ; kk = (int)(zz+0.5f) ;
     vv[pp] = FAR(ii,jj,kk) ;
   }
   return ;
}

/*---------------------------------------------------------------------------*/

static void GA_interp_linear( MRI_IMAGE *fim ,
                              int npp, float *ip, float *jp, float *kp, float *vv )
{
   int nx=fim->nx , ny=fim->ny , nz=fim->nz , nxy=nx*ny , pp ;
   float nxh=nx-0.501f , nyh=ny-0.501f , nzh=nz-0.501f , xx,yy,zz ;
   float fx,fy,fz ;
   float *far = MRI_FLOAT_PTR(fim) ;
   int nx1=nx-1,ny1=ny-1,nz1=nz-1, ix,jy,kz ;
   int ix_00,ix_p1 ;         /* interpolation indices */
   int jy_00,jy_p1 ;
   int kz_00,kz_p1 ;
   float wt_00,wt_p1 ;       /* interpolation weights */
   float f_j00_k00, f_jp1_k00, f_j00_kp1, f_jp1_kp1, f_k00, f_kp1 ;

   for( pp=0 ; pp < npp ; pp++ ){
     xx = ip[pp] ; if( xx < -0.499f || xx > nxh ){ vv[pp]=0.0f; continue; }
     yy = jp[pp] ; if( yy < -0.499f || yy > nyh ){ vv[pp]=0.0f; continue; }
     zz = kp[pp] ; if( zz < -0.499f || zz > nzh ){ vv[pp]=0.0f; continue; }

     ix = floorf(xx) ;  fx = xx - ix ;   /* integer and       */
     jy = floorf(yy) ;  fy = yy - jy ;   /* fractional coords */
     kz = floorf(zz) ;  fz = zz - kz ;

     ix_00 = ix ; ix_p1 = ix+1 ; CLIP(ix_00,nx1) ; CLIP(ix_p1,nx1) ;
     jy_00 = jy ; jy_p1 = jy+1 ; CLIP(jy_00,ny1) ; CLIP(jy_p1,ny1) ;
     kz_00 = kz ; kz_p1 = kz+1 ; CLIP(kz_00,nz1) ; CLIP(kz_p1,nz1) ;

     wt_00 = 1.0f-fx ; wt_p1 = fx ;  /* weights for ix_00 and ix_p1 points */

#undef  XINT
#define XINT(j,k) wt_00*FAR(ix_00,j,k)+wt_p1*FAR(ix_p1,j,k)

     /* interpolate to location ix+fx at each jy,kz level */

     f_j00_k00 = XINT(jy_00,kz_00) ; f_jp1_k00 = XINT(jy_p1,kz_00) ;
     f_j00_kp1 = XINT(jy_00,kz_p1) ; f_jp1_kp1 = XINT(jy_p1,kz_p1) ;

     /* interpolate to jy+fy at each kz level */

     wt_00 = 1.0f-fy ; wt_p1 = fy ;
     f_k00 =  wt_00 * f_j00_k00 + wt_p1 * f_jp1_k00 ;
     f_kp1 =  wt_00 * f_j00_kp1 + wt_p1 * f_jp1_kp1 ;

     /* interpolate to kz+fz to get output */

     vv[pp] = (1.0f-fz) * f_k00 + fz * f_kp1 ;
   }
   return ;
}

/*---------------------------------------------------------------------------*/

/* define Lagrange cubic interpolation polynomials */

#undef  P_M1
#undef  P_00
#undef  P_P1
#undef  P_P2
#undef  P_FACTOR
#define P_M1(x)  (-(x)*((x)-1)*((x)-2))
#define P_00(x)  (3*((x)+1)*((x)-1)*((x)-2))
#define P_P1(x)  (-3*(x)*((x)+1)*((x)-2))
#define P_P2(x)  ((x)*((x)+1)*((x)-1))
#define P_FACTOR 4.62962963e-3            /* 1/216 = final scaling factor */

static void GA_interp_cubic( MRI_IMAGE *fim ,
                             int npp, float *ip, float *jp, float *kp, float *vv )
{
   int nx=fim->nx , ny=fim->ny , nz=fim->nz , nxy=nx*ny , pp ;
   float nxh=nx-0.501f , nyh=ny-0.501f , nzh=nz-0.501f , xx,yy,zz ;
   float fx,fy,fz ;
   float *far = MRI_FLOAT_PTR(fim) ;
   int nx1=nx-1,ny1=ny-1,nz1=nz-1, ix,jy,kz ;

   int ix_m1,ix_00,ix_p1,ix_p2 ;     /* interpolation indices */
   int jy_m1,jy_00,jy_p1,jy_p2 ;
   int kz_m1,kz_00,kz_p1,kz_p2 ;
   float wt_m1,wt_00,wt_p1,wt_p2 ;   /* interpolation weights */
   float f_jm1_km1, f_j00_km1, f_jp1_km1, f_jp2_km1, /* interpolants */
         f_jm1_k00, f_j00_k00, f_jp1_k00, f_jp2_k00,
         f_jm1_kp1, f_j00_kp1, f_jp1_kp1, f_jp2_kp1,
         f_jm1_kp2, f_j00_kp2, f_jp1_kp2, f_jp2_kp2,
         f_km1    , f_k00    , f_kp1    , f_kp2     ;

   for( pp=0 ; pp < npp ; pp++ ){
     xx = ip[pp] ; if( xx < -0.499f || xx > nxh ){ vv[pp]=0.0f; continue; }
     yy = jp[pp] ; if( yy < -0.499f || yy > nyh ){ vv[pp]=0.0f; continue; }
     zz = kp[pp] ; if( zz < -0.499f || zz > nzh ){ vv[pp]=0.0f; continue; }

     ix = floorf(xx) ;  fx = xx - ix ;   /* integer and       */
     jy = floorf(yy) ;  fy = yy - jy ;   /* fractional coords */
     kz = floorf(zz) ;  fz = zz - kz ;

     ix_m1 = ix-1    ; ix_00 = ix      ; ix_p1 = ix+1    ; ix_p2 = ix+2    ;
     CLIP(ix_m1,nx1) ; CLIP(ix_00,nx1) ; CLIP(ix_p1,nx1) ; CLIP(ix_p2,nx1) ;

     jy_m1 = jy-1    ; jy_00 = jy      ; jy_p1 = jy+1    ; jy_p2 = jy+2    ;
     CLIP(jy_m1,ny1) ; CLIP(jy_00,ny1) ; CLIP(jy_p1,ny1) ; CLIP(jy_p2,ny1) ;

     kz_m1 = kz-1    ; kz_00 = kz      ; kz_p1 = kz+1    ; kz_p2 = kz+2    ;
     CLIP(kz_m1,nz1) ; CLIP(kz_00,nz1) ; CLIP(kz_p1,nz1) ; CLIP(kz_p2,nz1) ;

     wt_m1 = P_M1(fx) ; wt_00 = P_00(fx) ;  /* interpolation weights */
     wt_p1 = P_P1(fx) ; wt_p2 = P_P2(fx) ;

#undef  XINT
#define XINT(j,k) wt_m1*FAR(ix_m1,j,k)+wt_00*FAR(ix_00,j,k)  \
                 +wt_p1*FAR(ix_p1,j,k)+wt_p2*FAR(ix_p2,j,k)

     /* interpolate to location ix+fx at each jy,kz level */

     f_jm1_km1 = XINT(jy_m1,kz_m1) ; f_j00_km1 = XINT(jy_00,kz_m1) ;
     f_jp1_km1 = XINT(jy_p1,kz_m1) ; f_jp2_km1 = XINT(jy_p2,kz_m1) ;
     f_jm1_k00 = XINT(jy_m1,kz_00) ; f_j00_k00 = XINT(jy_00,kz_00) ;
     f_jp1_k00 = XINT(jy_p1,kz_00) ; f_jp2_k00 = XINT(jy_p2,kz_00) ;
     f_jm1_kp1 = XINT(jy_m1,kz_p1) ; f_j00_kp1 = XINT(jy_00,kz_p1) ;
     f_jp1_kp1 = XINT(jy_p1,kz_p1) ; f_jp2_kp1 = XINT(jy_p2,kz_p1) ;
     f_jm1_kp2 = XINT(jy_m1,kz_p2) ; f_j00_kp2 = XINT(jy_00,kz_p2) ;
     f_jp1_kp2 = XINT(jy_p1,kz_p2) ; f_jp2_kp2 = XINT(jy_p2,kz_p2) ;

     /* interpolate to jy+fy at each kz level */

     wt_m1 = P_M1(fy) ; wt_00 = P_00(fy) ;
     wt_p1 = P_P1(fy) ; wt_p2 = P_P2(fy) ;

     f_km1 =  wt_m1 * f_jm1_km1 + wt_00 * f_j00_km1
            + wt_p1 * f_jp1_km1 + wt_p2 * f_jp2_km1 ;
     f_k00 =  wt_m1 * f_jm1_k00 + wt_00 * f_j00_k00
            + wt_p1 * f_jp1_k00 + wt_p2 * f_jp2_k00 ;
     f_kp1 =  wt_m1 * f_jm1_kp1 + wt_00 * f_j00_kp1
            + wt_p1 * f_jp1_kp1 + wt_p2 * f_jp2_kp1 ;
     f_kp2 =  wt_m1 * f_jm1_kp2 + wt_00 * f_j00_kp2
            + wt_p1 * f_jp1_kp2 + wt_p2 * f_jp2_kp2 ;

     /* interpolate to kz+fz to get output */

     wt_m1 = P_M1(fz) ; wt_00 = P_00(fz) ;
     wt_p1 = P_P1(fz) ; wt_p2 = P_P2(fz) ;

     vv[pp] = P_FACTOR * (  wt_m1 * f_km1 + wt_00 * f_k00
                          + wt_p1 * f_kp1 + wt_p2 * f_kp2 ) ;
   }
   return ;
}


/*---------------------------------------------------------------------------*/

/* define quintic interpolation polynomials (Lagrange) */

#undef  Q_M2
#undef  Q_M1
#undef  Q_00
#undef  Q_P1
#undef  Q_P2
#undef  Q_P3
#define Q_M2(x)  (x*(x*x-1.0)*(2.0-x)*(x-3.0)*0.008333333)
#define Q_M1(x)  (x*(x*x-4.0)*(x-1.0)*(x-3.0)*0.041666667)
#define Q_00(x)  ((x*x-4.0)*(x*x-1.0)*(3.0-x)*0.083333333)
#define Q_P1(x)  (x*(x*x-4.0)*(x+1.0)*(x-3.0)*0.083333333)
#define Q_P2(x)  (x*(x*x-1.0)*(x+2.0)*(3.0-x)*0.041666667)
#define Q_P3(x)  (x*(x*x-1.0)*(x*x-4.0)*0.008333333)

static void GA_interp_quintic( MRI_IMAGE *fim ,
                               int npp, float *ip, float *jp, float *kp, float *vv )
{
   int nx=fim->nx , ny=fim->ny , nz=fim->nz , nxy=nx*ny , pp ;
   float nxh=nx-0.501f , nyh=ny-0.501f , nzh=nz-0.501f , xx,yy,zz ;
   float fx,fy,fz ;
   float *far = MRI_FLOAT_PTR(fim) ;
   int nx1=nx-1,ny1=ny-1,nz1=nz-1, ix,jy,kz ;
   int ix_m2,ix_m1,ix_00,ix_p1,ix_p2,ix_p3 ; /* interpolation indices */
   int jy_m2,jy_m1,jy_00,jy_p1,jy_p2,jy_p3 ; /* (input image) */
   int kz_m2,kz_m1,kz_00,kz_p1,kz_p2,kz_p3 ;

   float wt_m2,wt_m1,wt_00,wt_p1,wt_p2,wt_p3 ; /* interpolation weights */

   float f_jm2_km2, f_jm1_km2, f_j00_km2, f_jp1_km2, f_jp2_km2, f_jp3_km2,
         f_jm2_km1, f_jm1_km1, f_j00_km1, f_jp1_km1, f_jp2_km1, f_jp3_km1,
         f_jm2_k00, f_jm1_k00, f_j00_k00, f_jp1_k00, f_jp2_k00, f_jp3_k00,
         f_jm2_kp1, f_jm1_kp1, f_j00_kp1, f_jp1_kp1, f_jp2_kp1, f_jp3_kp1,
         f_jm2_kp2, f_jm1_kp2, f_j00_kp2, f_jp1_kp2, f_jp2_kp2, f_jp3_kp2,
         f_jm2_kp3, f_jm1_kp3, f_j00_kp3, f_jp1_kp3, f_jp2_kp3, f_jp3_kp3,
         f_km2    , f_km1    , f_k00    , f_kp1    , f_kp2    , f_kp3     ;

   for( pp=0 ; pp < npp ; pp++ ){
     xx = ip[pp] ; if( xx < -0.499f || xx > nxh ){ vv[pp]=0.0f; continue; }
     yy = jp[pp] ; if( yy < -0.499f || yy > nyh ){ vv[pp]=0.0f; continue; }
     zz = kp[pp] ; if( zz < -0.499f || zz > nzh ){ vv[pp]=0.0f; continue; }

     ix = floorf(xx) ;  fx = xx - ix ;   /* integer and       */
     jy = floorf(yy) ;  fy = yy - jy ;   /* fractional coords */
     kz = floorf(zz) ;  fz = zz - kz ;

     /* compute indexes from which to interpolate (-2,-1,0,+1,+2,+3),
        but clipped to lie inside input image volume                 */

     ix_m1 = ix-1    ; ix_00 = ix      ; ix_p1 = ix+1    ; ix_p2 = ix+2    ;
     CLIP(ix_m1,nx1) ; CLIP(ix_00,nx1) ; CLIP(ix_p1,nx1) ; CLIP(ix_p2,nx1) ;
     ix_m2 = ix-2    ; ix_p3 = ix+3 ;
     CLIP(ix_m2,nx1) ; CLIP(ix_p3,nx1) ;

     jy_m1 = jy-1    ; jy_00 = jy      ; jy_p1 = jy+1    ; jy_p2 = jy+2    ;
     CLIP(jy_m1,ny1) ; CLIP(jy_00,ny1) ; CLIP(jy_p1,ny1) ; CLIP(jy_p2,ny1) ;
     jy_m2 = jy-2    ; jy_p3 = jy+3 ;
     CLIP(jy_m2,ny1) ; CLIP(jy_p3,ny1) ;

     kz_m1 = kz-1    ; kz_00 = kz      ; kz_p1 = kz+1    ; kz_p2 = kz+2    ;
     CLIP(kz_m1,nz1) ; CLIP(kz_00,nz1) ; CLIP(kz_p1,nz1) ; CLIP(kz_p2,nz1) ;
     kz_m2 = kz-2    ; kz_p3 = kz+3 ;
     CLIP(kz_m2,nz1) ; CLIP(kz_p3,nz1) ;

     wt_m1 = Q_M1(fx) ; wt_00 = Q_00(fx) ;  /* interpolation weights */
     wt_p1 = Q_P1(fx) ; wt_p2 = Q_P2(fx) ;  /* in x-direction        */
     wt_m2 = Q_M2(fx) ; wt_p3 = Q_P3(fx) ;

#undef  XINT
#define XINT(j,k) wt_m2*FAR(ix_m2,j,k)+wt_m1*FAR(ix_m1,j,k) \
                 +wt_00*FAR(ix_00,j,k)+wt_p1*FAR(ix_p1,j,k) \
                 +wt_p2*FAR(ix_p2,j,k)+wt_p3*FAR(ix_p3,j,k)

     /* interpolate to location ix+fx at each jy,kz level */

     f_jm2_km2 = XINT(jy_m2,kz_m2) ; f_jm1_km2 = XINT(jy_m1,kz_m2) ;
     f_j00_km2 = XINT(jy_00,kz_m2) ; f_jp1_km2 = XINT(jy_p1,kz_m2) ;
     f_jp2_km2 = XINT(jy_p2,kz_m2) ; f_jp3_km2 = XINT(jy_p3,kz_m2) ;

     f_jm2_km1 = XINT(jy_m2,kz_m1) ; f_jm1_km1 = XINT(jy_m1,kz_m1) ;
     f_j00_km1 = XINT(jy_00,kz_m1) ; f_jp1_km1 = XINT(jy_p1,kz_m1) ;
     f_jp2_km1 = XINT(jy_p2,kz_m1) ; f_jp3_km1 = XINT(jy_p3,kz_m1) ;

     f_jm2_k00 = XINT(jy_m2,kz_00) ; f_jm1_k00 = XINT(jy_m1,kz_00) ;
     f_j00_k00 = XINT(jy_00,kz_00) ; f_jp1_k00 = XINT(jy_p1,kz_00) ;
     f_jp2_k00 = XINT(jy_p2,kz_00) ; f_jp3_k00 = XINT(jy_p3,kz_00) ;

     f_jm2_kp1 = XINT(jy_m2,kz_p1) ; f_jm1_kp1 = XINT(jy_m1,kz_p1) ;
     f_j00_kp1 = XINT(jy_00,kz_p1) ; f_jp1_kp1 = XINT(jy_p1,kz_p1) ;
     f_jp2_kp1 = XINT(jy_p2,kz_p1) ; f_jp3_kp1 = XINT(jy_p3,kz_p1) ;

     f_jm2_kp2 = XINT(jy_m2,kz_p2) ; f_jm1_kp2 = XINT(jy_m1,kz_p2) ;
     f_j00_kp2 = XINT(jy_00,kz_p2) ; f_jp1_kp2 = XINT(jy_p1,kz_p2) ;
     f_jp2_kp2 = XINT(jy_p2,kz_p2) ; f_jp3_kp2 = XINT(jy_p3,kz_p2) ;

     f_jm2_kp3 = XINT(jy_m2,kz_p3) ; f_jm1_kp3 = XINT(jy_m1,kz_p3) ;
     f_j00_kp3 = XINT(jy_00,kz_p3) ; f_jp1_kp3 = XINT(jy_p1,kz_p3) ;
     f_jp2_kp3 = XINT(jy_p2,kz_p3) ; f_jp3_kp3 = XINT(jy_p3,kz_p3) ;

     /* interpolate to jy+fy at each kz level */

     wt_m1 = Q_M1(fy) ; wt_00 = Q_00(fy) ; wt_p1 = Q_P1(fy) ;
     wt_p2 = Q_P2(fy) ; wt_m2 = Q_M2(fy) ; wt_p3 = Q_P3(fy) ;

     f_km2 =  wt_m2 * f_jm2_km2 + wt_m1 * f_jm1_km2 + wt_00 * f_j00_km2
            + wt_p1 * f_jp1_km2 + wt_p2 * f_jp2_km2 + wt_p3 * f_jp3_km2 ;

     f_km1 =  wt_m2 * f_jm2_km1 + wt_m1 * f_jm1_km1 + wt_00 * f_j00_km1
            + wt_p1 * f_jp1_km1 + wt_p2 * f_jp2_km1 + wt_p3 * f_jp3_km1 ;

     f_k00 =  wt_m2 * f_jm2_k00 + wt_m1 * f_jm1_k00 + wt_00 * f_j00_k00
            + wt_p1 * f_jp1_k00 + wt_p2 * f_jp2_k00 + wt_p3 * f_jp3_k00 ;

     f_kp1 =  wt_m2 * f_jm2_kp1 + wt_m1 * f_jm1_kp1 + wt_00 * f_j00_kp1
            + wt_p1 * f_jp1_kp1 + wt_p2 * f_jp2_kp1 + wt_p3 * f_jp3_kp1 ;

     f_kp2 =  wt_m2 * f_jm2_kp2 + wt_m1 * f_jm1_kp2 + wt_00 * f_j00_kp2
            + wt_p1 * f_jp1_kp2 + wt_p2 * f_jp2_kp2 + wt_p3 * f_jp3_kp2 ;

     f_kp3 =  wt_m2 * f_jm2_kp3 + wt_m1 * f_jm1_kp3 + wt_00 * f_j00_kp3
            + wt_p1 * f_jp1_kp3 + wt_p2 * f_jp2_kp3 + wt_p3 * f_jp3_kp3 ;

     /* interpolate to kz+fz to get output */

     wt_m1 = Q_M1(fz) ; wt_00 = Q_00(fz) ; wt_p1 = Q_P1(fz) ;
     wt_p2 = Q_P2(fz) ; wt_m2 = Q_M2(fz) ; wt_p3 = Q_P3(fz) ;

     vv[pp] =  wt_m2 * f_km2 + wt_m1 * f_km1 + wt_00 * f_k00
             + wt_p1 * f_kp1 + wt_p2 * f_kp2 + wt_p3 * f_kp3 ;
   }
   return ;
}

/*---------------------------------------------------------------------------*/

#undef  NPER
#define NPER 1024

static void GA_get_warped_values( int nmpar , double *mpar , float *avm )
{
   int    npar , nfree , ii,jj,kk,qq,pp,npp,mm,nx,ny,nxy , clip=0 ;
   float *wpar ;
   float *imf , *jmf , *kmf ;
   float *imw , *jmw , *kmw ;

   npar  = stup->wfunc_numpar ;
   nfree = stup->wfunc_numfree ;
   wpar  = (float *)malloc(sizeof(float)*npar) ;

   for( ii=pp=0 ; ii < npar ; ii++ ){
     wpar[ii] = ( stup->wfunc_param[ii].fixed )
               ? stup->wfunc_param[ii].val_fixed
               : (float)mpar[pp++] ;
   }

   if( stup->im == NULL ){
     imf = (float *)malloc(sizeof(float)*NPER) ;
     jmf = (float *)malloc(sizeof(float)*NPER) ;
     kmf = (float *)malloc(sizeof(float)*NPER) ;
   }
   imw = (float *)malloc(sizeof(float)*NPER) ;
   jmw = (float *)malloc(sizeof(float)*NPER) ;
   kmw = (float *)malloc(sizeof(float)*NPER) ;

   nx = stup->bsim->nx; ny = stup->bsim->ny; nxy = nx*ny;

   for( pp=0 ; pp < stup->npt_match ; pp+=NPER ){
     npp = MIN( NPER , stup->npt_match-pp ) ;
     if( stup->im == NULL ){
       for( qq=0 ; qq < npp ; qq++ ){
         mm = pp+qq ;
         ii = mm % nx; kk = mm / nxy; jj = (mm-kk*nxy) / nx;
         imf[qq] = ii; jmf[qq] = jj; kmf[qq] = kk;
       }
     } else {
       imf = stup->im + pp ;
       jmf = stup->jm + pp ;
       kmf = stup->km + pp ;
     }
     stup->wfunc( npar , wpar ,
                  npp  , imf,jmf,kmf , imw,jmw,kmw ) ;

     switch( stup->interp_code ){
       case MRI_NN:
         GA_interp_NN( stup->ajim , npp , imw,jmw,kmw , avm+pp ) ;
       break ;

       case MRI_LINEAR:
         GA_interp_linear( stup->ajim , npp , imw,jmw,kmw , avm+pp ) ;
       break ;

       case MRI_CUBIC:
         clip = 1 ;
         GA_interp_cubic( stup->ajim , npp , imw,jmw,kmw , avm+pp ) ;
       break ;

       default:        /* for higher order methods not implemented herein */
       case MRI_QUINTIC:
         clip = 1 ;
         GA_interp_quintic( stup->ajim , npp , imw,jmw,kmw , avm+pp ) ;
       break ;
     }
   }

   free((void *)kmw); free((void *)jmw); free((void *)imw);
   if( stup->im == NULL ){
     free((void *)kmf); free((void *)jmf); free((void *)imf);
   }
   free((void *)wpar) ;

   if( clip ){
     float bb=stup->ajbot , tt=stup->ajtop ;
     for( pp=0 ; pp < stup->npt_match ; pp++ )
            if( avm[pp] < bb ) avm[pp] = bb ;
       else if( avm[pp] > tt ) avm[pp] = tt ;
   }

   return ;
}

/*---------------------------------------------------------------------------*/

double GA_scalar_fitter( int npar , double *mpar )
{
  float val=0.0f ;
  float *avm , *bvm ;

  avm = (float *)malloc(stup->npt_match*sizeof(float)) ;
  GA_get_warped_values( npar , mpar , avm ) ;
  bvm = stup->bvm ;

  switch( stup->match_code ){

    default:
    case GA_MATCH_PEARSON_SCALAR:
      val = (double)THD_pearson_corr( stup->npt_match , avm , bvm ) ;
      val = 1.0 - fabs(val) ;
    break ;

    case GA_MATCH_SPEARMAN_SCALAR:
      val = (double)spearman_rank_corr( stup->npt_match, avm,
                                        stup->bvstat   , bvm ) ;
      val = 1.0 - fabs(val) ;
    break ;

    case GA_MATCH_KULLBACK_SCALAR:   /* not yet implemented */
    break ;
  }

  free((void *)avm) ;
  return (double)val ;
}

/*---------------------------------------------------------------------------*/

#undef  ERREX
#define ERREX(s) \
 do{ ERROR_message("mri_genalign_scalar: %s",(s)); RETURN(NULL); } while(0)

/*---------------------------------------------------------------------------*/

MRI_IMAGE * mri_genalign_scalar( MRI_IMAGE *basim  ,
                                 MRI_IMAGE *maskim ,
                                 MRI_IMAGE *targim ,
                                 GA_parameters *parm    )
{
   int nspad , qq , rr , nx,ny,nz,nxy , mm,ii,jj,kk ;
   int use_all=0 ;
   float *bsar ;
   double *wpar ;

ENTRY("mri_genalign_scalar") ;

   /*-- basic checks of input for rationality --*/

   if( basim  == NULL ) ERREX("basim is NULL") ;
   if( targim == NULL ) ERREX("targim is NULL") ;
   if( parm   == NULL ) ERREX("parm is NULL") ;

   nspad = MRI_DIMENSIONALITY(basim) ;
   if( nspad < 2 || nspad > 3 )
     ERREX("basim dimensionality is not 2 or 3") ;
   if( nspad != MRI_DIMENSIONALITY(targim) )
     ERREX("basim & targim dimensionalities differ") ;
   if( maskim != NULL && maskim->nvox != basim->nvox )
     ERREX("basim and maskim grids differ") ;

   if( parm->wfunc_numpar < 1 || parm->wfunc==NULL || parm->wfunc_param==NULL )
     ERREX("illegal wfunc parameters") ;

   FREE_GA_setup(stup) ;
   stup = (GA_setup *)calloc(1,sizeof(GA_setup)) ;

   stup->match_code    = parm->match_code    ;
   stup->interp_code   = parm->interp_code   ;
   stup->npt_match     = parm->npt_match     ;
   stup->kernel_code   = parm->kernel_code   ;
   stup->kernel_radius = parm->kernel_radius ;
   stup->npt_sum       = parm->npt_sum       ;
   stup->wfunc_numpar  = parm->wfunc_numpar  ;
   stup->wfunc         = parm->wfunc         ;

   stup->dim_avec = stup->dim_bvec = 1 ;  /* scalars */

   /* copy parameter definitions */

   { int nfree=stup->wfunc_numpar , pp , *pma ;

     stup->wfunc_param = (GA_param *)malloc(sizeof(GA_param)*stup->wfunc_numpar);
     for( pp=0 ; pp < stup->wfunc_numpar ; pp++ ){
       stup->wfunc_param[pp] = parm->wfunc_param[pp] ;
       if( stup->wfunc_param[pp].fixed ) nfree-- ;
     }
     if( nfree <= 0 ) ERREX("no free wfunc parameters") ;
     stup->wfunc_numfree = nfree ;

     /** pma[k] = external parameter index for the k-th free parameter **/

     pma = (int *)malloc(sizeof(int) * nfree) ;
     for( pp=ii=0 ; ii < stup->wfunc_numpar ; ii++ )
       if( !stup->wfunc_param[ii].fixed ) pma[pp++] = ii ;
     stup->wfunc_pma = pma ;
   }

   /** load images into setup struct, smoothing if so ordered **/

   stup->bsim = mri_to_float(basim ) ;
   stup->ajim = mri_to_float(targim) ;

   nx = stup->bsim->nx; ny = stup->bsim->ny; nz = stup->bsim->nz; nxy = nx*ny;

   if( parm->smooth_code > 0 && parm->smooth_radius > 0.0f ){
     MRI_IMAGE *qim ;
     qim = GA_smooth( stup->bsim , parm->smooth_code , parm->smooth_radius ) ;
     if( qim != NULL ){ mri_free(stup->bsim) ; stup->bsim = qim ; }
   }

   if( parm->smooth_code > 0 && parm->smooth_radius > 0.0f ){
     MRI_IMAGE *qim ;
     float nxa=stup->ajim->nx, nya=stup->ajim->ny, nza=stup->ajim->nz ;
     float rad=cbrtf(nxa*nya*nza/(nx*ny*nz)) * parm->smooth_radius ;
     qim = GA_smooth( stup->ajim , parm->smooth_code , rad ) ;
     if( qim != NULL ){ mri_free(stup->ajim) ; stup->ajim = qim ; }
   }
   stup->ajbot = (float)mri_min(stup->ajim) ;
   stup->ajtop = (float)mri_max(stup->ajim) ;

   /** load mask array **/

   if( maskim != NULL ){
     MRI_IMAGE *qim = mri_to_byte(maskim) ;
     stup->bmask = MRI_BYTE_PTR(qim) ;
     mri_fix_data_pointer( NULL , qim ) ;
     mri_free(qim) ;
     stup->nmask = THD_countmask( maskim->nvox , stup->bmask ) ;
     if( stup->nmask < 99 ){
       WARNING_message("mri_genalign_scalar: illegal input mask") ;
       free(stup->bmask) ; stup->bmask = NULL ; stup->nmask = 0 ;
     }
   } else {
     stup->bmask = NULL ;
     stup->nmask = 0 ;
   }

   /*-- extract matching points from base image --*/

   if( stup->npt_match <= 9 || stup->npt_match > stup->bsim->nvox ){
     stup->npt_match = stup->bsim->nvox ; use_all = 1 ;
   }
   if( stup->nmask > 0 && stup->npt_match > stup->nmask ){
     stup->npt_match = stup->nmask ; use_all = 2 ;
   }

   if( use_all == 1 ){         /*------------- all points, no mask -----------*/

     stup->im = stup->jm = stup->km = NULL ;

   } else if( use_all == 2 ){  /*------------- all points in mask ------------*/

     int nvox , pp ; byte *mask = stup->bmask ;

     stup->im = (float *)malloc(sizeof(float)*stup->npt_match) ;
     stup->jm = (float *)malloc(sizeof(float)*stup->npt_match) ;
     stup->km = (float *)malloc(sizeof(float)*stup->npt_match) ;

     for( mm=pp=0 ; pp < stup->npt_match ; mm++ ){
       if( GOOD(mm) ){
         ii = mm % nx; kk = mm / nxy; jj = (mm-kk*nxy) / nx;
         stup->im[pp] = ii; stup->jm[pp] = jj; stup->km[pp] = kk;
         pp++ ;
       }
     }

   } else {  /*--------------------- a subset of points ----------------------*/

     int nvox,pp,dm , *qm ; byte *mask = stup->bmask ;

     nvox = stup->bsim->nvox ;
     dm   = find_relprime_fixed(nvox) ;
     stup->im = (float *)malloc(sizeof(float)*stup->npt_match) ;
     stup->jm = (float *)malloc(sizeof(float)*stup->npt_match) ;
     stup->km = (float *)malloc(sizeof(float)*stup->npt_match) ;

     qm = (int *)malloc(sizeof(int)*stup->npt_match) ;
     mm = (nx/2) + (ny/2)*nx + (nz/2)*nxy ;
     for( pp=0 ; pp < stup->npt_match ; mm=(mm+dm)%nvox )
       if( GOOD(mm) ) qm[pp++] = mm ;
     qsort_int( stup->npt_match , qm ) ;

     for( pp=0 ; pp < stup->npt_match ; pp++ ){
       mm = qm[pp] ;
       ii = mm % nx; kk = mm / nxy; jj = (mm-kk*nxy) / nx;
       stup->im[pp] = ii; stup->jm[pp] = jj; stup->km[pp] = kk;
     }
     free((void *)qm) ;
   }

   /*--- extract values from base image for matching ---*/

   bsar = MRI_FLOAT_PTR(stup->bsim) ;
   stup->bvm = (float *)malloc(sizeof(float)*stup->npt_match) ;
   for( qq=0 ; qq < stup->npt_match ; qq++ ){
     rr = (stup->im != NULL)
          ?  (int)(stup->im[qq] + stup->jm[qq]*nx + stup->km[qq]*nxy) : qq ;
     stup->bvm[qq] = bsar[rr] ;
   }
   if( stup->match_code == GA_MATCH_SPEARMAN_SCALAR )
     stup->bvstat = spearman_rank_prepare( stup->npt_match , stup->bvm ) ;

   /**----- can now optimize warping parameters -----**/

   wpar = (double *)malloc(sizeof(double)*stup->wfunc_numfree) ;
   for( ii=qq=0 ; qq < stup->wfunc_numpar ; qq++ )
     if( !stup->wfunc_param[qq].fixed )
       wpar[ii++] = stup->wfunc_param[ii].val_init ;

   powell_newuoa( stup->wfunc_numfree , wpar ,
                  1.0 , 0.05 , 6666 , GA_scalar_fitter ) ;

   for( ii=qq=0 ; qq < stup->wfunc_numpar ; qq++ )
     if( stup->wfunc_param[qq].fixed )
       parm->wfunc_param[qq].val_out = parm->wfunc_param[qq].val_fixed ;
     else
       parm->wfunc_param[qq].val_out = wpar[ii++] ;

   RETURN(NULL) ;
}
