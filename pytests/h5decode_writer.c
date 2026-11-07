/* Fixtures for native HDF5 chunk decoding. GPL-2.0-only; see COPYING. */
#include <hdf5.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if H5_VERSION_GE(2,0,0)
#define NX 258
#define NY 130
#define NZ 8
static const hsize_t chunk[3]={2,64,128};
static void check(int ok,const char *s) { if(!ok) { fprintf(stderr,"fixture: %s\n",s);exit(1); } }
static double sample(int t,int z,int y,int x) {
  if(!z && !y && x<3)return x==0 ? -9999 : x==1 ? NAN : INFINITY;
  if(z==1 && y==2 && x==4)return NAN;
  return 10000*t+100*z+10*y+x+0.125;
}
static void filters(hid_t prop,unsigned skipped,int extra) {
  if(extra)check(H5Pset_nbit(prop)>=0,"nbit");
  if(!(skipped&1))check(H5Pset_shuffle(prop)>=0,"shuffle");
  if(!(skipped&2))check(H5Pset_deflate(prop,1)>=0,"deflate");
  if(!(skipped&4))check(H5Pset_fletcher32(prop)>=0,"checksum");
}
static void chunk_data(float *data,const hsize_t *origin) {
  size_t at=0;hsize_t z,y,x;
  for(z=0;z<chunk[0];z++)for(y=0;y<chunk[1];y++)for(x=0;x<chunk[2];x++)
    data[at++]=(origin[0]+z<NZ && origin[1]+y<NY && origin[2]+x<NX) ?
      (float)sample(0,(int)(origin[0]+z),(int)(origin[1]+y),(int)(origin[2]+x)) : -9999;
}
int main(int argc,char **argv) {
  const char *names[]={"float","double","rank4","permuted","bigendian","integer","packed","sparse","skipped","extra","partial","corrupt","badzip","legacy","contiguous","uncompressed"};
  char path[4096];size_t variant;
  check(argc==2,"directory argument");
  for(variant=0;variant<sizeof(names)/sizeof(names[0]);variant++) {
    hid_t file,space,prop,dataset,type,memory;size_t at=0,n;int t,z,y,x;
    int rank=variant==2 ? 4 : 3,nt=variant==2 ? 2 : 1;
    hsize_t dims[4]={nt,NZ,NY,NX},chunks[4]={1,2,64,128},rdims[1]={NZ};
    float *f=NULL,fill=-9999;double *g=NULL;short *s=NULL;
    snprintf(path,sizeof(path),"%s/%s.h5",argv[1],names[variant]);
    if(variant==3) { dims[1]=NY;dims[2]=NX;dims[3]=NZ;chunks[1]=64;chunks[2]=128;chunks[3]=2; }
    file=H5Fcreate(path,H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);check(file>=0,"file");
    space=H5Screate_simple(rank,dims+(rank==3),NULL);prop=H5Pcreate(H5P_DATASET_CREATE);
    if(variant!=14)check(H5Pset_chunk(prop,rank,chunks+(rank==3))>=0,"chunks");
    if(variant<14)filters(prop,0,variant==9);
    if(variant==10)check(H5Pset_chunk_opts(prop,H5D_CHUNK_DONT_FILTER_PARTIAL_CHUNKS)>=0,"partial chunks");
    type=variant==1 ? H5T_NATIVE_DOUBLE : variant==4 ? H5T_IEEE_F32BE : variant==5 ? H5T_NATIVE_SHORT : H5T_NATIVE_FLOAT;
    memory=variant==1 ? H5T_NATIVE_DOUBLE : variant==5 ? H5T_NATIVE_SHORT : H5T_NATIVE_FLOAT;
    check(H5Pset_fill_value(prop,H5T_NATIVE_FLOAT,&fill)>=0,"fill");
    dataset=H5Dcreate2(file,"f",type,space,H5P_DEFAULT,prop,H5P_DEFAULT);check(dataset>=0,"dataset");
    n=(size_t)nt*NZ*NY*NX;
    if(variant==1)g=malloc(n*sizeof(*g));else if(variant==5)s=malloc(n*sizeof(*s));else f=malloc(n*sizeof(*f));
    check(f || g || s,"field memory");
    if(variant==3) {
      for(y=0;y<NY;y++)for(x=0;x<NX;x++)for(z=0;z<NZ;z++)f[at++]=(float)sample(0,z,y,x);
    } else for(t=0;t<nt;t++)for(z=0;z<NZ;z++)for(y=0;y<NY;y++)for(x=0;x<NX;x++,at++) {
      double value=sample(t,z,y,x);
      if(g)g[at]=value;
      else if(s)s[at]=isfinite(value) ? (short)value : -9999;
      else f[at]=(float)value;
    }
    if(variant==7) {
      hsize_t origin[3]={0,0,0};hid_t selected=H5Screate_simple(3,chunk,NULL);
      float *part=malloc((size_t)chunk[0]*chunk[1]*chunk[2]*sizeof(float));check(part!=NULL,"sparse memory");
      chunk_data(part,origin);check(H5Sselect_hyperslab(space,H5S_SELECT_SET,origin,NULL,chunk,NULL)>=0,"sparse selection");
      check(H5Dwrite(dataset,H5T_NATIVE_FLOAT,selected,space,H5P_DEFAULT,part)>=0,"sparse write");
      free(part);H5Sclose(selected);
    } else check(H5Dwrite(dataset,memory,H5S_ALL,H5S_ALL,H5P_DEFAULT,g ? (void *)g : s ? (void *)s : (void *)f)>=0,"field write");
    if(variant==6) {
      double scale=0.125,add=10;hid_t scalar=H5Screate(H5S_SCALAR),attr;
      attr=H5Acreate2(dataset,"scale_factor",H5T_NATIVE_DOUBLE,scalar,H5P_DEFAULT,H5P_DEFAULT);check(H5Awrite(attr,H5T_NATIVE_DOUBLE,&scale)>=0,"scale");H5Aclose(attr);
      attr=H5Acreate2(dataset,"add_offset",H5T_NATIVE_DOUBLE,scalar,H5P_DEFAULT,H5P_DEFAULT);check(H5Awrite(attr,H5T_NATIVE_DOUBLE,&add)>=0,"offset");H5Aclose(attr);H5Sclose(scalar);
    }
    free(f);free(g);free(s);H5Dclose(dataset);H5Pclose(prop);H5Sclose(space);
    {
      double rho[NZ];for(z=0;z<NZ;z++)rho[z]=0.5+z*0.125;
      space=H5Screate_simple(1,rdims,NULL);dataset=H5Dcreate2(file,"rho",H5T_NATIVE_DOUBLE,space,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
      check(dataset>=0 && H5Dwrite(dataset,H5T_NATIVE_DOUBLE,H5S_ALL,H5S_ALL,H5P_DEFAULT,rho)>=0,"rho");H5Dclose(dataset);H5Sclose(space);
    }
    H5Fclose(file);
    if(variant==8 || (variant>=11 && variant<=13)) {
      file=H5Fopen(path,H5F_ACC_RDWR,H5P_DEFAULT);dataset=H5Dopen2(file,"f",H5P_DEFAULT);
      check(dataset>=0,"reopen raw dataset");
      if(variant==8) {
        unsigned mask;
        for(mask=0;mask<8;mask++) {
          hid_t encoded;hsize_t origin[3]={2*(mask/4),64*((mask/2)%2),128*(mask%2)},zero[3]={0,0,0};
          unsigned actual;size_t capacity;hsize_t packed;char name[32];void *raw;
          float *part=malloc((size_t)chunk[0]*chunk[1]*chunk[2]*sizeof(float));check(part!=NULL,"encode memory");chunk_data(part,origin);
          snprintf(name,sizeof(name),"encode%u",mask);space=H5Screate_simple(3,chunk,NULL);prop=H5Pcreate(H5P_DATASET_CREATE);
          check(H5Pset_chunk(prop,3,chunk)>=0,"encode chunks");filters(prop,mask,0);
          encoded=H5Dcreate2(file,name,H5T_NATIVE_FLOAT,space,H5P_DEFAULT,prop,H5P_DEFAULT);check(encoded>=0,"encode dataset");
          check(H5Dwrite(encoded,H5T_NATIVE_FLOAT,H5S_ALL,H5S_ALL,H5P_DEFAULT,part)>=0,"encode data");free(part);
          check(H5Dflush(encoded)>=0 && H5Dget_chunk_storage_size(encoded,zero,&packed)>=0,"encoded size");
          raw=malloc((size_t)packed);check(raw!=NULL,"encoded memory");capacity=(size_t)packed;
          check(H5Dread_chunk2(encoded,H5P_DEFAULT,zero,&actual,raw,&capacity)>=0,"encoded read");
          check(H5Dwrite_chunk(dataset,H5P_DEFAULT,mask,origin,capacity,raw)>=0,"write skipped filters");
          free(raw);H5Dclose(encoded);H5Pclose(prop);H5Sclose(space);
        }
      } else {
        hsize_t origin[3]={0,0,0},packed;unsigned mask;size_t capacity;unsigned char *raw;
        check(H5Dget_chunk_storage_size(dataset,origin,&packed)>=0 && packed>4,"corrupt size");
        capacity=(size_t)packed;raw=malloc(capacity);check(raw!=NULL,"corrupt memory");
        check(H5Dread_chunk2(dataset,H5P_DEFAULT,origin,&mask,raw,&capacity)>=0,"corrupt read");
        if(variant==12) { capacity-=4;mask|=4;raw[0]^=255; }
        else if(variant==13) {
          unsigned char swap=raw[capacity-4];raw[capacity-4]=raw[capacity-3];raw[capacity-3]=swap;
          swap=raw[capacity-2];raw[capacity-2]=raw[capacity-1];raw[capacity-1]=swap;
        } else raw[capacity-1]^=1;
        check(H5Dwrite_chunk(dataset,H5P_DEFAULT,mask,origin,capacity,raw)>=0,"corrupt write");free(raw);
      }
      H5Dclose(dataset);H5Fclose(file);
    }
  }
  return 0;
}
#else
int main(void) { puts("SKIP: raw chunk tests require HDF5 2.0");return 77; }
#endif
