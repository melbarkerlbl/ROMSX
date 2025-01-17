#ifndef STRETCH_H_
#define STRETCH_H_

#include <cmath>
#include <DataStruct.H>
#include <ROMSX.H>
#include <prob_common.H>

using namespace amrex;

void
ROMSX::set_2darrays (int lev)
{
    auto& lev_new = vars_new[lev];
    auto& lev_old = vars_old[lev];
    std::unique_ptr<MultiFab>& mf_x_r = vec_x_r[lev];
    std::unique_ptr<MultiFab>& mf_y_r = vec_y_r[lev];
    auto N = Geom(lev).Domain().size()[2]-1; // Number of vertical "levs" aka, NZ

    for ( MFIter mfi(*(mf_x_r), TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {

      Array4<Real> const& x_r = (mf_x_r)->array(mfi);
      Array4<Real> const& y_r = (mf_y_r)->array(mfi);
      const Box& bx = mfi.growntilebox();
      const auto & geomdata = Geom(lev).data();
      int ncomp = 1;
      Gpu::synchronize();
      amrex::ParallelFor(bx, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
      {
        const auto prob_lo         = geomdata.ProbLo();
        const auto dx              = geomdata.CellSize();

        x_r(i,j,0) = prob_lo[0] + (i + 0.5) * dx[0];
        y_r(i,j,0) = prob_lo[1] + (j + 0.5) * dx[1];
        //        const Real z = prob_lo[2] + (k + 0.5) * dx[2];

      });
    }
    //    x_r[lev]->FillBoundary(geom[lev].periodicity());
    //    y_r[lev]->FillBoundary(geom[lev].periodicity());

    MultiFab& U_old = vars_new[lev][Vars::xvel];
    MultiFab& V_old = vars_new[lev][Vars::yvel];
    MultiFab& W_old = vars_new[lev][Vars::zvel];
    std::unique_ptr<MultiFab>& mf_ubar = vec_ubar[lev];
    std::unique_ptr<MultiFab>& mf_vbar = vec_vbar[lev];
    std::unique_ptr<MultiFab>& mf_Hz  = vec_Hz[lev];
    int nstp = 0;
    int kstp = 0;
    int knew = 0;
    int ncomp = 1;
    for ( MFIter mfi(U_old, TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Array4<Real> const& ubar = (mf_ubar)->array(mfi);
        Array4<Real> const& vbar = (mf_vbar)->array(mfi);
        Array4<Real> const& Hz       = (mf_Hz)->array(mfi);
        Array4<Real> const& u        = (U_old).array(mfi);
        Array4<Real> const& v        = (V_old).array(mfi);

        Box bx = mfi.tilebox();
        //copy the tilebox
        Box gbx1 = bx;
        Box gbx11 = bx;
        Box gbx2 = bx;
        //make only gbx be grown to match multifabs
        gbx2.grow(IntVect(NGROW,NGROW,0));
        gbx1.grow(IntVect(NGROW-1,NGROW-1,0));
        gbx11.grow(IntVect(NGROW-1,NGROW-1,NGROW-1));
        Box gbx1D = gbx1;
        gbx1D.makeSlab(2,0);

        FArrayBox fab_DC(gbx11,1,amrex::The_Async_Arena());
        FArrayBox fab_CF(gbx11,1,amrex::The_Async_Arena());
        auto DC=fab_DC.array();
        auto CF=fab_CF.array();

        //fab_DC.setVal(0.0);
        //fab_CF.setVal(0.0);
        amrex::ParallelFor(gbx11, ncomp,
        [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
            {
                DC(i,j,k)=0.0;
                CF(i,j,k)=0.0;
            });
      Gpu::streamSynchronize();
      amrex::ParallelFor(gbx1D, ncomp,
        [=] AMREX_GPU_DEVICE (int i, int j, int , int )
            {
                for(int k=0; k<=N; k++) {
                DC(i,j,k)=0.5*(Hz(i,j,k)+Hz(i-1,j,k));
                DC(i,j,-1)=DC(i,j,-1)+DC(i,j,k);
                CF(i,j,-1)=CF(i,j,-1)+DC(i,j,k)*u(i,j,k,nstp);
                }
            });
        amrex::ParallelFor(gbx1,
        [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                Real cff1=1.0/DC(i,j,-1);
                Real cff2=CF(i,j,-1)*cff1;
                ubar(i,j,0,kstp)=cff2;
                ubar(i,j,0,knew)=cff2;
            });

        //fab_DC.setVal(0.0);
        //fab_CF.setVal(0.0);
        amrex::ParallelFor(gbx11, ncomp,
        [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
            {
                DC(i,j,k)=0.0;
                CF(i,j,k)=0.0;
            });
      Gpu::streamSynchronize();
        amrex::ParallelFor(gbx1D,
        [=] AMREX_GPU_DEVICE (int i, int j, int )
            {
                for(int k=0; k<=N; k++) {
                DC(i,j,k)=0.5*(Hz(i,j,k)+Hz(i,j-1,k));
                DC(i,j,-1)=DC(i,j,-1)+DC(i,j,k);
                CF(i,j,-1)=CF(i,j,-1)+DC(i,j,k)*v(i,j,k,nstp);
                }
            });
        amrex::ParallelFor(gbx1,
        [=] AMREX_GPU_DEVICE (int i, int j, int k)
            {
                Real cff1=1.0/DC(i,j,-1);
                Real cff2=CF(i,j,-1)*cff1;
                vbar(i,j,0,kstp)=cff2;
                vbar(i,j,0,knew)=cff2;
            });
    }
    vec_ubar[lev]->FillBoundary(geom[lev].periodicity());
    vec_vbar[lev]->FillBoundary(geom[lev].periodicity());
}

void ROMSX::set_vmix (int lev)
{
    auto& lev_new = vars_new[lev];
    auto& lev_old = vars_old[lev];
    std::unique_ptr<MultiFab>& mf_Akv = vec_Akv[lev];
    std::unique_ptr<MultiFab>& mf_z_w = vec_z_w[lev];

    for ( MFIter mfi(*(mf_Akv), TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
      Array4<Real> const& Akv = (mf_Akv)->array(mfi);
      Array4<Real> const& z_w = (mf_z_w)->array(mfi);
      Box bx = mfi.tilebox();
      bx.grow(IntVect(NGROW,NGROW,0));
      const auto & geomdata = Geom(lev).data();
      int ncomp = 1;
      Gpu::streamSynchronize();
      amrex::ParallelFor(bx, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
      {

        Akv(i,j,k) = 2.0e-03+8.0e-03*std::exp(z_w(i,j,k)/150.0);

      });
    }
}

void
ROMSX::set_smflux(int lev, Real time)
{
    std::unique_ptr<MultiFab>& mf_sustr = vec_sustr[lev];
    std::unique_ptr<MultiFab>& mf_svstr = vec_svstr[lev];
    auto geomdata = Geom(lev).data();
    bool NSPeriodic = geomdata.isPeriodic(1);
    bool EWPeriodic = geomdata.isPeriodic(0);
    //If we had wind stress and bottom stress we would need to set these:
    Real pi = 3.14159265359;
    Real tdays=time/(24.0*60.0*60.0);
    amrex::Print()<<"Hacking in time offset for fixed dt=300"<<std::endl;
    //this is a hack because time is off by dt. this needs to be fixed for non-fixed dt
    Real dstart=0.0;//-300.0/(24.0*60.0*60.0);
    Real rho0=parms.rho0;
    Real windamp;
    amrex::Print()<<tdays<<" "<<dstart<<" "<<rho0<<std::endl;
    //It's possible these should be set to be nonzero only at the boundaries they affect
    if(NSPeriodic) {
        mf_sustr->setVal(0.0);
    }
    else if(EWPeriodic) {
        if ((tdays-dstart)<=2.0)
            windamp=-0.1*sin(pi*(tdays-dstart)/4.0)/rho0;
        else
            windamp=-0.1/rho0;
        mf_sustr->setVal(windamp);
    }
    if(NSPeriodic) {
        if ((tdays-dstart)<=2.0)
            windamp=-0.1*sin(pi*(tdays-dstart)/4.0)/rho0;
        else
            windamp=-0.1/rho0;
        mf_svstr->setVal(windamp);
    }
    else if(EWPeriodic) {
        mf_svstr->setVal(0.0);
    }

}
void
ROMSX::set_depth (int lev)
{
    auto& lev_new = vars_new[lev];
    auto& lev_old = vars_old[lev];
    std::unique_ptr<MultiFab>& mf_z_w = vec_z_w[lev];
    std::unique_ptr<MultiFab>& mf_z_r = vec_z_r[lev];
    std::unique_ptr<MultiFab>& mf_s_r = vec_s_r[lev];
    std::unique_ptr<MultiFab>& mf_Hz  = vec_Hz[lev];
    std::unique_ptr<MultiFab>& mf_h  = vec_hOfTheConfusingName[lev];
    std::unique_ptr<MultiFab>& mf_z_phys_nd  = vec_z_phys_nd[lev];
    mf_h->setVal(geom[lev].ProbHi(2));
    Real depth = geom[lev].ProbHi(2);
    const int Lm = Geom(lev).Domain().size()[0];
    const int Mm = Geom(lev).Domain().size()[1];
    std::unique_ptr<MultiFab>& mf_Zt_avg1  = vec_Zt_avg1[lev];
    mf_Zt_avg1->setVal(0.0);
    auto N_loop = Geom(lev).Domain().size()[2]-1; // Number of vertical "levs" aka, NZ

    for ( MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {

      Array4<Real> const& z_w = (mf_z_w)->array(mfi);
      Array4<Real> const& z_r = (mf_z_r)->array(mfi);
      Array4<Real> const& s_r = (mf_s_r)->array(mfi);
      Array4<Real> const& z_phys_nd  = (mf_z_phys_nd)->array(mfi);
      Array4<Real> const& Hz  = (mf_Hz)->array(mfi);
      Box bx = mfi.tilebox();
      Box gbx2 = bx;
      gbx2.grow(IntVect(NGROW,NGROW,0));
      Box wgbx2 = gbx2.surroundingNodes(2);
      Box gbx21 = bx;
      gbx21.grow(IntVect(NGROW,NGROW,NGROW-1));
      Box gbx22 = bx;
      gbx22.grow(IntVect(NGROW,NGROW,NGROW));
      Box gbx2D = gbx2;
      gbx2D.makeSlab(2,0);

      Array4<Real> const& h  = (mf_h)->array(mfi);
      Array4<Real> const& Zt_avg1  = (mf_Zt_avg1)->array(mfi);
      const auto & geomdata = Geom(lev).data();

      int ncomp = 1;
      Box subdomain;
      if (lev == 0) {
          subdomain = geom[lev].Domain();
      } else {
          amrex::Abort("Geometry information needs to be updated for multilevel");
      }

      int nx = subdomain.length(0);
      int ny = subdomain.length(1);
      int nz = subdomain.length(2);

      auto N = nz; // Number of vertical "levels" aka, NZ
      //forcing tcline to be the same as probhi for now, one in DataStruct.H other in inputs
      Real hc=-min(geomdata.ProbHi(2),-solverChoice.tcline); // Do we need to enforce min here?
      bool NSPeriodic = geomdata.isPeriodic(1);
      bool EWPeriodic = geomdata.isPeriodic(0);
      const auto local_theta_s = solverChoice.theta_s;
      const auto local_theta_b = solverChoice.theta_b;

      if(!solverChoice.flat_bathymetry) {
      Gpu::streamSynchronize();
      amrex::ParallelFor(wgbx2, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
      {
          Real val1, val2;
          int iFort = i+1;
          int jFort = j+1;
          if(NSPeriodic) {
              if (iFort<=Lm/2.0)
                  val1=iFort;
              else
                  val1=Lm+1-iFort;
              val2=min(-geomdata.ProbLo(2),(84.5+66.526*std::tanh((val1-10.0)/7.0)));
              h(i,j,0) = val2;
          }
          else if(EWPeriodic) {
              if (jFort<=Mm/2.0)
                  val1=jFort;
              else
                  val1=Mm+1-jFort;
              val2=min(-geomdata.ProbLo(2),(84.5+66.526*std::tanh((val1-10.0)/7.0)));
              h(i,j,0) = val2;
          }
          z_w(i,j,k) = h(i,j,0);
      });
      } else {
      Gpu::streamSynchronize();
      amrex::ParallelFor(wgbx2, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
      {
          h(i,j,0) = -geomdata.ProbLo(2);
          z_w(i,j,k) = h(i,j,0);
          if(k==0)
              h(i,j,0,1) = h(i,j,0);
      });
      }

      // ROMS Transform 2
      Gpu::streamSynchronize();
      amrex::ParallelFor(gbx2D, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int , int )
      {
        for(int k=-1; k<=N_loop; k++) {
        //        const Real z = prob_lo[2] + (k + 0.5) * dx[2];
        const auto prob_lo         = geomdata.ProbLo();
        const auto dx              = geomdata.CellSize();
        //This is the z for the bottom of the cell this k corresponds to
        //   if we weren't stretching and transforming
        const Real z = prob_lo[2] + (k) * dx[2];
        //        h(i,j,0) = -prob_lo[2]; // conceptually

        ////////////////////////////////////////////////////////////////////
        //ROMS Stretching 4
        // Move this block to it's own function for maintainability if needed
        // Information about the problem dimension would need to be added
        // This file would need a k dependent function to return the
        // stretching scalars, or access to 4 vectors of length prob_length(2)
        /////////////////////////////////////////////////////////////////////
        Real ds = 1.0_rt / Real(N);

        Real cff_r, cff_w, cff1_r, cff1_w, cff2_r, cff2_w, Csur, Cbot;
        Real sc_r,sc_w,Cs_r,Cs_w;

        if(k==N) // end of array // pretend we're storing 0?
        {
        sc_w=0.0; //sc_w / hc
        Cs_w=0.0; //Cs_w
        }
        else if (k==0) // beginning of array
        {
        sc_w=-1.0; //sc_w / hc
        Cs_w=-1.0; //Cs_w
        }
        else
        {
          sc_w=ds*(k-N);

          if (local_theta_s > 0.0_rt) {
            Csur=(1.0_rt-std::cosh(local_theta_s*sc_w))/
              (std::cosh(local_theta_s)-1.0_rt);
          } else {
            Csur=-sc_w*sc_w;
          }

          if (local_theta_b > 0.0_rt) {
            Cbot=(std::exp(local_theta_b*Csur)-1.0_rt)/
              (1.0_rt-std::exp(-local_theta_b));
            Cs_w=Cbot;
          } else {
            Cs_w=Csur;
          }
        }

        cff_w=hc*sc_w;
        cff1_w=Cs_w;

        //cff_r => sc_r *hc
        //cff1_r => Cs_r
        //Don't do anything special for first/last index
        {
          sc_r=ds*(k-N+0.5_rt);

          if (local_theta_s > 0.0_rt) {
            Csur=(1.0_rt-std::cosh(local_theta_s*sc_r))/
              (std::cosh(local_theta_s)-1.0_rt);
          } else {
            Csur=-sc_r*sc_r;
          }

          if (local_theta_b > 0.0_rt) {
            Cbot=(std::exp(local_theta_b*Csur)-1.0_rt)/
              (1.0_rt-std::exp(-local_theta_b));
            Cs_r=Cbot;
          } else {
            Cs_r=Csur;
          }
        }

        if (i==0&&j==0&&k<N&&k>=0) {
            s_r(0,0,k) = sc_r;
        }

        cff_r=hc*sc_r;
        cff1_r=Cs_r;

        ////////////////////////////////////////////////////////////////////
        Real hwater=h(i,j,0);
        /*
        if(k==0)
         {
          //extra guess added (maybe not actually defined in ROMS)
        Real hinv=1.0_rt/(hc+hwater);
         cff2_r=(cff_r+cff1_r*hwater)*hinv;
         //         z_w(i,j,k-2) = hwater;
         //         z_w(i,j,k-1)= -hwater;

         z_r(i,j,k) = Zt_avg1(i,j,0)+(Zt_avg1(i,j,0)+hwater)*cff2_r;
         Hz(i,j,k)=z_w(i,j,k)+hwater;//-z_w(i,j,k-1);
         }
        else
        */
        {
        //Note, we are not supporting ICESHELF flag
          Real hinv=1.0_rt/(hc+hwater);
          cff2_r=(cff_r+cff1_r*hwater)*hinv;
          cff2_w=(cff_w+cff1_w*hwater)*hinv;

          if(k==0) {
              z_w(i,j,N-1)=0.0;
          }
          else if(k==-1)
              h(i,j,0,1)=Zt_avg1(i,j,0)+(Zt_avg1(i,j,0)+hwater)*cff2_w;
          else
              z_w(i,j,k-1)=Zt_avg1(i,j,0)+(Zt_avg1(i,j,0)+hwater)*cff2_w;
          if(k!=-1)
          z_r(i,j,k)=Zt_avg1(i,j,0)+(Zt_avg1(i,j,0)+hwater)*cff2_r;
        }
        }
      });
      Gpu::streamSynchronize();
      amrex::ParallelFor(gbx2D, ncomp,
      [=] AMREX_GPU_DEVICE (int i, int j, int , int )
      {
          for(int k=0; k<=N_loop; k++) {
          if(k==0) {
              Hz(i,j,k)=z_w(i,j,k)+h(i,j,0);
          }
          else
              Hz(i,j,k)=z_w(i,j,k)-z_w(i,j,k-1);
          }
      });
    }

    vec_z_w[lev]->FillBoundary(geom[lev].periodicity());
    vec_z_r[lev]->FillBoundary(geom[lev].periodicity());
    vec_s_r[lev]->FillBoundary(geom[lev].periodicity());
    vec_Hz[lev]->FillBoundary(geom[lev].periodicity());
    vec_hOfTheConfusingName[lev]->FillBoundary(geom[lev].periodicity());
    vec_Zt_avg1[lev]->FillBoundary(geom[lev].periodicity());

    // Define nodal z as average of z on w-faces
    for ( MFIter mfi(lev_new[Vars::cons], TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
      Array4<Real> const& z_w = (mf_z_w)->array(mfi);
      Array4<Real> const& z_phys_nd  = (mf_z_phys_nd)->array(mfi);

      Box z_w_box = Box(z_w);
      auto const lo = amrex::lbound(z_w_box);
      auto const hi = amrex::ubound(z_w_box);

      amrex::ParallelFor(Box(z_phys_nd),
      [=] AMREX_GPU_DEVICE (int i, int j, int k)
      {
          // For now assume all boundaries are constant height --
          //     we will enforce periodicity below
          if ( i >= lo.x && i <= hi.x-1 && j >= lo.y && j <= hi.y-1 )
          {
              z_phys_nd(i,j,k)=0.25*( z_w(i,j  ,k) + z_w(i+1,j  ,k) +
                                      z_w(i,j+1,k) + z_w(i+1,j+1,k) );
          } else {
             int ii = std::min(std::max(i, lo.x), hi.x);
             int jj = std::min(std::max(j, lo.y), hi.y);
             z_phys_nd(i,j,k) = z_w(ii,jj,k);
          }
      });
    } // mf

    // Enforce periodicity
    vec_z_phys_nd[lev]->FillBoundary(geom[lev].periodicity());
}

/**
 * Set horizontal mixing coefficients
 */
void ROMSX::set_hmixcoef (int lev) {
    std::unique_ptr<MultiFab>& mf_visc2_p = vec_visc2_p[lev];
    std::unique_ptr<MultiFab>& mf_visc2_r = vec_visc2_r[lev];
    std::unique_ptr<MultiFab>& mf_diff2_salt = vec_diff2_salt[lev];
    std::unique_ptr<MultiFab>& mf_diff2_temp = vec_diff2_temp[lev];

    for ( MFIter mfi(*(mf_visc2_p), TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Array4<Real> const& visc2_p = (mf_visc2_p)->array(mfi);
        Array4<Real> const& visc2_r = (mf_visc2_r)->array(mfi);
        Array4<Real> const& diff2_salt = (mf_diff2_salt)->array(mfi);
        Array4<Real> const& diff2_temp = (mf_diff2_temp)->array(mfi);
        Box bx = mfi.tilebox();
        bx.grow(IntVect(NGROW,NGROW,0));
        int ncomp = 1;

        Gpu::synchronize();
        amrex::ParallelFor(bx, ncomp,
        [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
        {
            // NOTE: Hard-coded value from ana_hmixcoef.h
            visc2_p(i,j,k) = 5.0;
            visc2_r(i,j,k) = 5.0;
            // Also defined in ana_hmixcoef.h but values for diffusion2 are set in mod_scalars.F (tl_tnu2)
            diff2_salt(i,j,k) = 0.0;
            diff2_temp(i,j,k) = 0.0;
        });
    }
}

/**
 * Set drag coefficients
 */
void ROMSX::set_drag (int lev) {
    std::unique_ptr<MultiFab>& mf_rdrag = vec_rdrag[lev];
    std::unique_ptr<MultiFab>& mf_hOfTheConfusingName = vec_hOfTheConfusingName[lev];

    for ( MFIter mfi(*(mf_rdrag), TilingIfNotGPU()); mfi.isValid(); ++mfi )
    {
        Array4<Real> const& rdrag = (mf_rdrag)->array(mfi);
        Array4<Real> const& hOfTheConfusingName = (mf_hOfTheConfusingName)->array(mfi);
        Box bx = mfi.tilebox();
        bx.grow(IntVect(NGROW,NGROW,0));
        int ncomp = 1;

        Gpu::synchronize();
        // Functional form given in ana_drag.h, ifdef UPWELLING and UV_LDRAG
        amrex::ParallelFor(bx, ncomp,
        [=] AMREX_GPU_DEVICE (int i, int j, int k, int n)
        {
            rdrag(i,j,k)=0.002*(1.0-std::tanh(hOfTheConfusingName(i,j,k)/150.));
        });
    }
}

//  This routine sets the weigth functions for the time averaging of
//  2D fields over all short time-steps.
void ROMSX::set_weights (int lev) {

    int i,j,iter;
    Real gamma, scale;
    Real cff1, cff2;
    Real wsum, shift, cff;

    //HACK should possibly store fixed_ndtfast elsewhere
    int ndtfast=fixed_ndtfast_ratio>0 ? fixed_ndtfast_ratio : fixed_fast_dt / fixed_dt;

    //From mod_scalars
    Real Falpha = 2.0_rt;
    Real Fbeta = 4.0_rt;
    Real Fgamma = 0.284_rt;

    vec_weight1.resize(2*ndtfast+1);
    vec_weight2.resize(2*ndtfast+1);

    auto weight1 = vec_weight1.dataPtr();
    auto weight2 = vec_weight2.dataPtr();

//
//=======================================================================
//  Compute time-averaging filter for barotropic fields.
//=======================================================================
//
//  Initialize both sets of weights to zero.
//
    nfast=0;
    for(int i=1;i<=2*ndtfast;i++) {
        weight1[i-1]=0.0_rt;
        weight2[i-1]=0.0_rt;
    }
//
//-----------------------------------------------------------------------
//  Power-law shape filters.
//-----------------------------------------------------------------------
//
//  The power-law shape filters are given by:
//
//     F(xi)=xi^Falpha*(1-xi^Fbeta)-Fgamma*xi
//
//  where xi=scale*i/ndtfast; and scale, Falpha, Fbeta, Fgamma, and
//  normalization are chosen to yield the correct zeroth-order
//  (normalization), first-order (consistency), and second-order moments,
//  resulting in overall second-order temporal accuracy for time-averaged
//  barotropic motions resolved by baroclinic time step.
//
      scale=(Falpha+1.0_rt)*(Falpha+Fbeta+1.0_rt)/
      ((Falpha+2.0_rt)*(Falpha+Fbeta+2.0_rt)*Real(ndtfast));
//
//  Find center of gravity of the primary weighting shape function and
//  iteratively adjust "scale" to place the  centroid exactly at
//  "ndtfast".
//
      gamma=Fgamma*max(0.0_rt, 1.0_rt-10.0_rt/Real(ndtfast));
      for(iter=1;iter<=16;iter++) {
          nfast=0;
      for(int i=1;i<=2*ndtfast;i++) {
          cff=scale*Real(i);
          weight1[i-1]=pow(cff,Falpha)-pow(cff,(Falpha+Fbeta))-gamma*cff;
          if (weight1[i-1]>0.0_rt) nfast=i;
          if ((nfast>0)&&(weight1[i-1]<0.0_rt)) {
            weight1[i-1]=0.0_rt;
          }
    }
        wsum=0.0_rt;
        shift=0.0_rt;
        for(i=1;i<=nfast;i++) {
          wsum=wsum+weight1[i-1];
          shift=shift+weight1[i-1]*Real(i);
        }
        scale=scale*shift/(wsum*Real(ndtfast));
      }
//
//-----------------------------------------------------------------------
//  Post-processing of primary weights.
//-----------------------------------------------------------------------
//
//  Although it is assumed that the initial settings of the primary
//  weights has its center of gravity "reasonably close" to NDTFAST,
//  it may be not so according to the discrete rules of integration.
//  The following procedure is designed to put the center of gravity
//  exactly to NDTFAST by computing mismatch (NDTFAST-shift) and
//  applying basically an upstream advection of weights to eliminate
//  the mismatch iteratively. Once this procedure is complete primary
//  weights are normalized.
//
//  Find center of gravity of the primary weights and subsequently
//  calculate the mismatch to be compensated.
//
      for(iter=1;iter<=ndtfast;iter++) {
        wsum=0.0_rt;
        shift=0.0_rt;
        for(i=1;i<=nfast;i++) {
          wsum=wsum+weight1[i-1];
          shift=shift+Real(i)*weight1[i-1];
        }
        shift=shift/wsum;
        cff=Real(ndtfast)-shift;
//
//  Apply advection step using either whole, or fractional shifts.
//  Notice that none of the four loops here is reversible.
//
        if (cff>1.0_rt) {
          nfast=nfast+1;
          for(i=nfast;i>=2;i--) {
            weight1[i-1]=weight1[i-1-1];
          }
          weight1[1-1]=0.0_rt;
        } else if (cff>0.0_rt) {
          wsum=1.0_rt-cff;
          for(i=nfast;i>=2;i--) {
          weight1[i-1]=wsum*weight1[i-1]+cff*weight1[i-1-1];
          }
          weight1[1-1]=wsum*weight1[1-1];
        } else if (cff<-1.0_rt) {
          nfast=nfast-1;
          for(i=1;i<=nfast;i++) {
          weight1[i-1]=weight1[i+1-1];
          }
          weight1[nfast+1-1]=0.0_rt;
        } else if (cff<0.0_rt) {
          wsum=1.0_rt+cff;
          for(i=1;i<=nfast-1;i++) {
          weight1[i-1]=wsum*weight1[i-1]-cff*weight1[i+1-1];
          }
          weight1[nfast-1]=wsum*weight1[nfast-1];
        }
      }
//
//  Set SECONDARY weights assuming that backward Euler time step is used
//  for free surface.  Notice that array weight2[i] is assumed to
//  have all-zero status at entry in this segment of code.
//
        for(j=1;j<=nfast;j++) {
        cff=weight1[j-1];
        for(i=1;i<=j;i++) {
            weight2[i-1]=weight2[i-1]+cff;
        }
      }
//
//  Normalize both set of weights.
//
      wsum=0.0_rt;
      cff=0.0_rt;
      for(i=1;i<=nfast;i++) {
        wsum=wsum+weight1[i-1];
        cff=cff+weight2[i-1];
      }
      wsum=1.0_rt/wsum;
      cff=1.0_rt/cff;
      for(i=1;i<=nfast;i++) {
        weight1[i-1]=wsum*weight1[i-1];
        weight2[i-1]=cff*weight2[i-1];
      }
//
//  Report weights.
//
    if (ParallelDescriptor::IOProcessor()) {
        Print().SetPrecision(18)<<ParallelDescriptor::NProcs()<<"  "<<ndtfast<<"  "<<nfast<<"  "<<wsum<<std::endl;
        cff=0.0_rt;
        cff1=0.0_rt;
        cff2=0.0_rt;
        wsum=0.0_rt;
        shift=0.0_rt;
        for(i=1;i<=nfast;i++) {
          cff=cff+weight1[i-1];
          cff1=cff1+weight1[i-1]*Real(i);
          cff2=cff2+weight1[i-1]*Real(i*i);
          wsum=wsum+weight2[i-1];
          shift=shift+weight2[i-1]*(Real(i)-0.5_rt);
          Print().SetPrecision(18)<<"i="<<i<<"  "<<weight1[i-1]<<"  "<<weight2[i-1]<<"  "<<cff<<"  "<<wsum<<std::endl;
        }
        cff1=cff1/Real(ndtfast);
        cff2=cff2/(Real(ndtfast)*Real(ndtfast));
        shift=shift/Real(ndtfast);
        Print().SetPrecision(18)<<ndtfast <<"  "<< nfast<<"  "<<Real(nfast)/Real(ndtfast)<<std::endl;
        Print().SetPrecision(18)<<cff1<<"  "<<cff2<<"  "<<shift<<"  "<<cff<<"  "<<wsum<<"  "<<Fgamma<<"  "<<gamma<<std::endl;
      if (cff2<1.0001_rt) Print()<<"\n\n\n"<<std::endl;
      }
}
#endif
