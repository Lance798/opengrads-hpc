/* Added in 2026 for cross-format ZSLICE and ZINT fixtures. GPLv2. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ZS_ADIOS
#include <adios2_c.h>
#endif
#ifdef ZS_NETCDF
#include <netcdf.h>
#endif
#ifdef ZS_HDF5
#include <hdf5.h>
#endif
#define NX 16
#define NY 8
#define NZ 4
#define NT 2
static float f[NT][NZ][NY][NX], indexmap[NT][NY][NX], surface[NT][NY][NX];
static double g[NT][NZ][NY][NX];
static void check(int condition, const char *message) {
  if (!condition) { fprintf(stderr,"zslice_writer: %s\n",message); exit(1); }
}

int main(int argc, char **argv) {
  int t,z,y,x;
  char path[4096];
  FILE *raw;
  check(argc==2 || (argc==3 && !strcmp(argv[2],"zint")),"expected fixture directory [zint]");
  for (t=0;t<NT;t++) for (y=0;y<NY;y++) for (x=0;x<NX;x++) {
    indexmap[t][y][x]=(float)(1+(x/2+y+t)%NZ);
    surface[t][y][x]=(float)(900+100*t+10*y+x);
    for (z=0;z<NZ;z++) {
      f[t][z][y][x]=(float)(1000*t+100*(z+1)+10*y+x);
      g[t][z][y][x]=(double)f[t][z][y][x]+0.125;
    }
  }
  for (t=0;t<NT;t++) {
    indexmap[t][0][0]=0; indexmap[t][0][1]=NZ+1;
    indexmap[t][0][2]=1.5f; indexmap[t][0][3]=-9999;
    indexmap[t][0][4]=NAN; indexmap[t][0][5]=INFINITY;
    f[t][0][2][4]=-9999;
  }
  if (argc==3) for (t=0;t<NT;t++) for (z=0;z<NZ;z++) {
    f[t][z][0][0]=-9999; g[t][z][0][0]=-9999;
    f[t][z][0][1]=NAN; g[t][z][0][1]=NAN;
    f[t][z][0][2]=INFINITY; g[t][z][0][2]=INFINITY;
    if (z==1) { f[t][z][0][3]=NAN; g[t][z][0][3]=NAN; }
  }
  snprintf(path,sizeof(path),"%s/slice.dat",argv[1]);
  raw=fopen(path,"wb"); check(raw!=NULL,"open binary fixture");
  for (t=0;t<NT;t++) {
    /* Both file fields are float in the binary fixture. */
    float fg[NZ][NY][NX];
    for (z=0;z<NZ;z++) for (y=0;y<NY;y++) for (x=0;x<NX;x++) fg[z][y][x]=(float)g[t][z][y][x];
    check(fwrite(f[t],sizeof(f[t]),1,raw)==1,"write f");
    check(fwrite(fg,sizeof(fg),1,raw)==1,"write g");
    check(fwrite(indexmap[t],sizeof(indexmap[t]),1,raw)==1,"write index");
    check(fwrite(surface[t],sizeof(surface[t]),1,raw)==1,"write surface");
    {
      FILE *piece;
      snprintf(path,sizeof(path),"%s/slice%d.dat",argv[1],t);
      piece=fopen(path,"wb"); check(piece!=NULL,"open templated binary");
      check(fwrite(f[t],sizeof(f[t]),1,piece)==1,"template f");
      check(fwrite(fg,sizeof(fg),1,piece)==1,"template g");
      check(fwrite(indexmap[t],sizeof(indexmap[t]),1,piece)==1,"template index");
      check(fwrite(surface[t],sizeof(surface[t]),1,piece)==1,"template surface");
      check(fclose(piece)==0,"close templated binary");
    }
  }
  check(fclose(raw)==0,"close binary fixture");
#ifdef ZS_NETCDF
  {
    int mode;
    for (mode=0;mode<4;mode++) {
      int file, dims[4], fvar,gvar,kvar,svar;
      int source=mode>=2 ? mode-2 : 0;
      const float fill=-9999;
      if (mode>=2) snprintf(path,sizeof(path),"%s/slice%d.nc",argv[1],source);
      else snprintf(path,sizeof(path),"%s/slice%s.nc",argv[1],mode ? "4" : "");
      check(nc_create(path,NC_CLOBBER|(mode ? NC_NETCDF4 : 0),&file)==NC_NOERR,"create NetCDF");
      check(nc_def_dim(file,"t",mode>=2 ? 1 : NT,dims)==NC_NOERR,"NetCDF time");
      check(nc_def_dim(file,"z",NZ,dims+1)==NC_NOERR,"NetCDF z");
      check(nc_def_dim(file,"y",NY,dims+2)==NC_NOERR,"NetCDF y");
      check(nc_def_dim(file,"x",NX,dims+3)==NC_NOERR,"NetCDF x");
      check(nc_def_var(file,"f",NC_FLOAT,4,dims,&fvar)==NC_NOERR,"NetCDF f");
      check(nc_def_var(file,"g",NC_DOUBLE,4,dims,&gvar)==NC_NOERR,"NetCDF g");
      { int kdims[3]={dims[0],dims[2],dims[3]};
        check(nc_def_var(file,"kmap",NC_FLOAT,3,kdims,&kvar)==NC_NOERR,"NetCDF kmap");
        check(nc_def_var(file,"surface",NC_FLOAT,3,kdims,&svar)==NC_NOERR,"NetCDF surface"); }
      check(nc_put_att_float(file,fvar,"_FillValue",NC_FLOAT,1,&fill)==NC_NOERR,"NetCDF fill");
      if (mode) {
        check(nc_def_var_deflate(file,fvar,1,1,3)==NC_NOERR,"NetCDF f compression");
        check(nc_def_var_deflate(file,gvar,1,1,3)==NC_NOERR,"NetCDF g compression");
      }
      check(nc_enddef(file)==NC_NOERR,"NetCDF enddef");
      check(nc_put_var_float(file,fvar,&f[source][0][0][0])==NC_NOERR,"NetCDF write f");
      check(nc_put_var_double(file,gvar,&g[source][0][0][0])==NC_NOERR,"NetCDF write g");
      check(nc_put_var_float(file,kvar,&indexmap[source][0][0])==NC_NOERR,"NetCDF write kmap");
      check(nc_put_var_float(file,svar,&surface[source][0][0])==NC_NOERR,"NetCDF write surface");
      check(nc_close(file)==NC_NOERR,"NetCDF close");
    }
  }
#endif
#ifdef ZS_HDF5
  {
    hid_t file;
    hsize_t dims[4]={NT,NZ,NY,NX}, kdims[3]={NT,NY,NX}, chunks[4]={1,2,4,8};
    hsize_t rdim[1]={NZ};
    const double rho[NZ]={1.2,1.1,1.0,0.9};
    const char *names[5]={"f","g","kmap","surface","rho"};
    int v,piece;
    for (piece=-1;piece<NT;piece++) {
    const void *data[5]={piece<0 ? (const void *)f : f[piece],
                        piece<0 ? (const void *)g : g[piece],
                        piece<0 ? (const void *)indexmap : indexmap[piece],
                        piece<0 ? (const void *)surface : surface[piece],rho};
    if (piece<0) snprintf(path,sizeof(path),"%s/slice.h5",argv[1]);
    else snprintf(path,sizeof(path),"%s/slice%d.h5",argv[1],piece);
    file=H5Fcreate(path,H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT); check(file>=0,"create HDF5");
    for (v=0;v<5;v++) {
      int offset=piece<0 ? 0 : 1;
      int rank=v<2 ? 4-offset : v<4 ? 3-offset : 1;
      hid_t space=H5Screate_simple(rank,v<2 ? dims+offset : v<4 ? kdims+offset : rdim,NULL);
      hid_t prop=H5Pcreate(H5P_DATASET_CREATE), var;
      hid_t type=v==1 || v==4 ? H5T_NATIVE_DOUBLE : H5T_NATIVE_FLOAT;
      if (v<2) { check(H5Pset_chunk(prop,rank,chunks+offset)>=0,"HDF5 chunks"); check(H5Pset_deflate(prop,3)>=0,"HDF5 compression"); }
      var=H5Dcreate2(file,names[v],type,space,H5P_DEFAULT,prop,H5P_DEFAULT); check(var>=0,"HDF5 variable");
      check(H5Dwrite(var,type,H5S_ALL,H5S_ALL,H5P_DEFAULT,data[v])>=0,"HDF5 write");
      H5Dclose(var); H5Pclose(prop); H5Sclose(space);
    }
    check(H5Fclose(file)>=0,"HDF5 close");
    }
  }
#endif
#ifdef ZS_ADIOS
  {
    adios2_adios *adios=adios2_init_serial();
    int compressed;
    check(adios!=NULL,"ADIOS init");
    for (compressed=0;compressed<2;compressed++) {
      const size_t shape[3]={NZ,NY,NX}, start[3]={0,0,0}, count[3]={2,NY,8};
      const size_t rshape[1]={NZ}, rstart[1]={0};
      const double rho[NZ]={1.2,1.1,1.0,0.9};
      adios2_io *io=adios2_declare_io(adios,compressed ? "zstd" : "raw");
      adios2_variable *fv,*gv,*kv,*sv,*rv;
      adios2_engine *engine;
      check(adios2_set_engine(io,"BP5")==adios2_error_none,"BP5 engine");
      fv=adios2_define_variable(io,"f",adios2_type_float,3,shape,start,count,adios2_constant_dims_false);
      gv=adios2_define_variable(io,"g",adios2_type_double,3,shape,start,count,adios2_constant_dims_false);
      kv=adios2_define_variable(io,"kmap",adios2_type_float,2,shape+1,start+1,shape+1,adios2_constant_dims_true);
      sv=adios2_define_variable(io,"surface",adios2_type_float,2,shape+1,start+1,shape+1,adios2_constant_dims_true);
      rv=adios2_define_variable(io,"rho",adios2_type_double,1,rshape,rstart,rshape,adios2_constant_dims_true);
      check(fv && gv && kv && sv && rv,"BP5 variables");
      if (compressed) {
        size_t opindex;
        adios2_operator *op=adios2_define_operator(adios,"lossless","blosc");
        check(op!=NULL,"Blosc operator");
        check(adios2_add_operation(&opindex,fv,op,"compressor","zstd")==adios2_error_none,"compress f");
        check(adios2_add_operation(&opindex,gv,op,"compressor","zstd")==adios2_error_none,"compress g");
      }
      snprintf(path,sizeof(path),"%s/slice%s.bp",argv[1],compressed ? "zstd" : "");
      engine=adios2_open(io,path,adios2_mode_write); check(engine!=NULL,"BP5 open");
      for (t=0;t<NT;t++) {
        adios2_step_status status;
        int bz,bx;
        check(adios2_begin_step(engine,adios2_step_mode_append,0,&status)==adios2_error_none && status==adios2_step_status_ok,"BP5 step");
        for (bz=0;bz<NZ;bz+=2) for (bx=0;bx<NX;bx+=8) {
          size_t selection[3]={(size_t)bz,0,(size_t)bx};
          float fb[2][NY][8]; double gb[2][NY][8];
          for (z=0;z<2;z++) for (y=0;y<NY;y++) for (x=0;x<8;x++) {
            fb[z][y][x]=f[t][z+bz][y][x+bx]; gb[z][y][x]=g[t][z+bz][y][x+bx]; }
          check(adios2_set_selection(fv,3,selection,count)==adios2_error_none,"BP5 f selection");
          check(adios2_set_selection(gv,3,selection,count)==adios2_error_none,"BP5 g selection");
          check(adios2_put(engine,fv,fb,adios2_mode_sync)==adios2_error_none,"BP5 f write");
          check(adios2_put(engine,gv,gb,adios2_mode_sync)==adios2_error_none,"BP5 g write");
        }
        check(adios2_put(engine,kv,indexmap[t],adios2_mode_sync)==adios2_error_none,"BP5 kmap write");
        check(adios2_put(engine,sv,surface[t],adios2_mode_sync)==adios2_error_none,"BP5 surface write");
        if (!t) check(adios2_put(engine,rv,rho,adios2_mode_sync)==adios2_error_none,"BP5 profile write");
        check(adios2_end_step(engine)==adios2_error_none,"BP5 end step");
      }
      check(adios2_close(engine)==adios2_error_none,"BP5 close");
    }
    check(adios2_finalize(adios)==adios2_error_none,"ADIOS finalize");
  }
#endif
  return 0;
}
