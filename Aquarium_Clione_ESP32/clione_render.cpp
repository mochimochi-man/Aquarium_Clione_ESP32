#pragma GCC optimize("O3")
#include "clione_render.h"
#include "clione_assets.h"
#include "gfx.h"
#include <math.h>

namespace {
static int bound(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
__attribute__((unused)) static void blend(int x, int y, uint16_t color, int alpha) {
  // No global dirty box: each rendering core writes only its own row band.
  uint16_t& p = FB[(size_t)fbRow(y) * FB_W + x];
  p = blend565(p, color, (uint32_t)bound(alpha, 0, 32));
}
#if CONFIG_IDF_TARGET_ESP32C3
// The C3 has no FPU, so per-pixel float sqrt/divide dominates these shapes.
// Every caller draws a short span, which keeps Q8 coordinates far inside
// int32; an integer square root replaces sqrtf entirely.
static int isqrt32(uint32_t n) {
  uint32_t res=0,bit=1u<<30;
  while(bit>n)bit>>=2;
  while(bit){
    if(n>=res+bit){n-=res+bit;res=(res>>1)+bit;}
    else res>>=1;
    bit>>=2;
  }
  return (int)res;
}
static void disc(float cx, float cy, float radius, uint16_t color, float opacity, int y0, int y1) {
  int left = bound((int)floorf(cx-radius-1), 0, FB_W);
  int right = bound((int)ceilf(cx+radius+1), 0, FB_W);
  int top = bound((int)floorf(cy-radius-1), y0, y1);
  int bottom = bound((int)ceilf(cy+radius+1), y0, y1);
  const int cxq=(int)lroundf(cx*256.f),cyq=(int)lroundf(cy*256.f);
  const int reach=(int)lroundf(radius*256.f)+128,opq=(int)lroundf(opacity*256.f);
  for (int y=top; y<bottom; ++y) {
    const int ry=((y<<8)+128)-cyq;
    uint16_t* dst=FB+(size_t)fbRow(y)*FB_W+left;
    for (int x=left; x<right; ++x,++dst) {
      const int rx=((x<<8)+128)-cxq;
      int cov=reach-isqrt32((uint32_t)(rx*rx+ry*ry));
      if(cov<=0)continue;
      if(cov>256)cov=256;
      const int alpha=(cov*opq*32+32768)>>16;
      if(alpha)*dst=blend565(*dst,color,(uint32_t)alpha);
    }
  }
}
static void line(float x0, float y0, float x1, float y1, float radius,
                 uint16_t color, float opacity, int band0, int band1) {
  int left=bound((int)floorf(fminf(x0,x1)-radius-1),0,FB_W);
  int right=bound((int)ceilf(fmaxf(x0,x1)+radius+1),0,FB_W);
  int top=bound((int)floorf(fminf(y0,y1)-radius-1),band0,band1);
  int bottom=bound((int)ceilf(fmaxf(y0,y1)+radius+1),band0,band1);
  const float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy);
  if(len<1e-3f)return;
  const int ux=(int)lroundf(dx/len*256.f),uy=(int)lroundf(dy/len*256.f);
  const int lenq=(int)lroundf(len*256.f);
  const int x0q=(int)lroundf(x0*256.f),y0q=(int)lroundf(y0*256.f);
  const int reach=(int)lroundf(radius*256.f)+128,opq=(int)lroundf(opacity*256.f);
  for(int y=top;y<bottom;++y){
    const int ry=((y<<8)+128)-y0q;
    uint16_t* dst=FB+(size_t)fbRow(y)*FB_W+left;
    for(int x=left;x<right;++x,++dst){
      const int rx=((x<<8)+128)-x0q;
      int t=(rx*ux+ry*uy)>>8;
      if(t<0)t=0;else if(t>lenq)t=lenq;
      const int ex=rx-((t*ux)>>8),ey=ry-((t*uy)>>8);
      int cov=reach-isqrt32((uint32_t)(ex*ex+ey*ey));
      if(cov<=0)continue;
      if(cov>256)cov=256;
      const int alpha=(cov*opq*32+32768)>>16;
      if(alpha)*dst=blend565(*dst,color,(uint32_t)alpha);
    }
  }
}
#else
static void disc(float cx, float cy, float radius, uint16_t color, float opacity, int y0, int y1) {
  int left = bound((int)floorf(cx-radius-1), 0, FB_W);
  int right = bound((int)ceilf(cx+radius+1), 0, FB_W);
  int top = bound((int)floorf(cy-radius-1), y0, y1);
  int bottom = bound((int)ceilf(cy+radius+1), y0, y1);
  for (int y=top; y<bottom; ++y) for (int x=left; x<right; ++x) {
    float dx=x+.5f-cx, dy=y+.5f-cy;
    float coverage=fminf(1, fmaxf(0, radius+.5f-sqrtf(dx*dx+dy*dy)));
    int alpha=(int)(coverage*opacity*32+.5f);
    if (alpha) blend(x,y,color,alpha);
  }
}
static void line(float x0, float y0, float x1, float y1, float radius,
                 uint16_t color, float opacity, int band0, int band1) {
  int left=bound((int)floorf(fminf(x0,x1)-radius-1),0,FB_W);
  int right=bound((int)ceilf(fmaxf(x0,x1)+radius+1),0,FB_W);
  int top=bound((int)floorf(fminf(y0,y1)-radius-1),band0,band1);
  int bottom=bound((int)ceilf(fmaxf(y0,y1)+radius+1),band0,band1);
  float dx=x1-x0,dy=y1-y0,d2=dx*dx+dy*dy;
  for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
    float t=d2>1e-8f?((x+.5f-x0)*dx+(y+.5f-y0)*dy)/d2:0;
    t=fminf(1,fmaxf(0,t));
    float ex=x+.5f-x0-t*dx,ey=y+.5f-y0-t*dy;
    float coverage=fminf(1,fmaxf(0,radius+.5f-sqrtf(ex*ex+ey*ey)));
    int alpha=(int)(coverage*opacity*32+.5f);if(alpha)blend(x,y,color,alpha);
  }
}
#endif
static void shell(const clione::Prey& p, int y0, int y1) {
  float x=p.x*FB_W,y=p.y*FB_H,r=p.size*FB_H;
  disc(x,y,r,rgb565(181,146,105),p.empty?.24f:.65f,y0,y1);
  float lx=x,ly=y;
  for(int i=1;i<=28;++i){
    float t=i/28.0f, a=t*10.5f+p.spin, rr=r*t;
    float nx=x+cosf(a)*rr,ny=y+sinf(a)*rr;
    line(lx,ly,nx,ny,fmaxf(.24f,FB_H*.0012f),rgb565(236,201,151),.82f,y0,y1);
    lx=nx;ly=ny;
  }
  // Aperture: the cones turn it toward the mouth before extraction.
  const float aa=clione::APERTURE_ANGLE+p.spin;
  disc(x+cosf(aa)*r*.74f,y+sinf(aa)*r*.74f,r*.37f,rgb565(104,79,65),.8f,y0,y1);
  if(!p.empty&&p.owner<0){
    line(x-r*.4f,y,x-r*1.4f,y-r*.3f,r*.17f,rgb565(170,211,220),.5f,y0,y1);
    line(x+r*.4f,y,x+r*1.4f,y-r*.3f,r*.17f,rgb565(170,211,220),.5f,y0,y1);
  }
}
// Deform the original spread-wing pose as a textured membrane. Using one
// source pose prevents the head, organs and silhouette from jumping per frame.
struct Vertex { float x,y,u,v; };
#if !CONFIG_IDF_TARGET_ESP32C3
static Vertex pose(const clione::Animal& a,float u,float v,float size,float c,float s,float wingPhase) {
  float x=u-.5f,y=v-.5f;
  if(v>.04f && v<.26f)x*=1+.25f*clione::extension(a)*sinf((v-.04f)/.22f*3.14159265f);
  float span=fmaxf(0,(fabsf(x)-.105f)/.395f);
  const float cyaw=cosf(a.yaw),syaw=sinf(a.yaw),bodyWidth=sqrtf(cyaw*cyaw+.6724f*syaw*syaw);
  bool wing=v>=.23f && v<=.54f && span>0;
  if(wing) {
    float side=x<0?-1:1;
    float phase=wingPhase-.7f*span+side*.035f;
    float amplitude=.68f+.24f*a.effort;
    x=side*.105f*bodyWidth+.80f*side*(fabsf(x)-.105f)*(.66f+.30f*cosf(phase))*cyaw
      +.16f*span*sinf(phase)*syaw;
    y=-.18f+(y+.18f)*(1-.18f*span);
    y+=.192f*amplitude*span*sinf(phase)
       +.04f*span*span*sinf(phase-.8f)*(v-.30f)/.24f;
  } else if(v>.54f) {
    float tail=(v-.54f)/.46f;
    x+=(.020f*sinf(wingPhase-.7f)-.08f*a.turn)*tail*tail;
  }
  if(!wing)x*=bodyWidth;
  return {a.x*FB_W+(c*x-s*y)*size,a.y*FB_H+(s*x+c*y)*size,u,v};
}
static float edge(const Vertex& a,const Vertex& b,float x,float y) {
  return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
static bool topLeft(const Vertex& a,const Vertex& b) {
  return b.y<a.y || (b.y==a.y && b.x>a.x);
}
#endif
#if CONFIG_IDF_TARGET_ESP32C3
// Subpixel edge tests and Q16 UV stepping avoid software floats per pixel.
// Shared quantized vertices plus the top-left rule prevent cracks/double blends.
static void triangle(Vertex a,Vertex b,Vertex c,int gain,float rear,int y0,int y1) {
  struct Point { int x,y; };
  auto point=[](const Vertex& v)->Point { return {(int)lroundf(v.x*16),(int)lroundf(v.y*16)}; };
  Point pa=point(a),pb=point(b),pc=point(c);
  auto cross=[](Point p,Point q,int x,int y)->int { return (q.x-p.x)*(y-p.y)-(q.y-p.y)*(x-p.x); };
  int area=cross(pa,pb,pc.x,pc.y);
  if(!area)return;
  if(area<0){Vertex v=b;b=c;c=v;Point p=pb;pb=pc;pc=p;area=-area;}
  int left=bound((int)floorf(fminf(a.x,fminf(b.x,c.x))),0,FB_W);
  int right=bound((int)ceilf(fmaxf(a.x,fmaxf(b.x,c.x))),0,FB_W);
  int top=bound((int)floorf(fminf(a.y,fminf(b.y,c.y))),y0,y1);
  int bottom=bound((int)ceilf(fmaxf(a.y,fmaxf(b.y,c.y))),y0,y1);
  if(left>=right||top>=bottom)return;
  const ClioneFrame& f=CLIONE_SWIM[2];
  const int dx0=(pb.y-pc.y)*16,dx1=(pc.y-pa.y)*16,dx2=(pa.y-pb.y)*16;
  const int dy0=(pc.x-pb.x)*16,dy1=(pa.x-pc.x)*16,dy2=(pb.x-pa.x)*16;
  int row0=cross(pb,pc,left*16+8,top*16+8);
  int row1=cross(pc,pa,left*16+8,top*16+8);
  int row2=cross(pa,pb,left*16+8,top*16+8);
  auto inclusive=[](Point p,Point q) { return q.y<p.y || (q.y==p.y && q.x>p.x); };
  const int bias0=inclusive(pb,pc)?0:-1,bias1=inclusive(pc,pa)?0:-1,bias2=inclusive(pa,pb)?0:-1;
  const float uscale=65536.0f*f.width/area,vscale=65536.0f*f.height/area;
  const int du=(int)lroundf((dx0*a.u+dx1*b.u+dx2*c.u)*uscale);
  const int dv=(int)lroundf((dx0*a.v+dx1*b.v+dx2*c.v)*vscale);
  const int duy=(int)lroundf((dy0*a.u+dy1*b.u+dy2*c.u)*uscale);
  const int dvy=(int)lroundf((dy0*a.v+dy1*b.v+dy2*c.v)*vscale);
  int rowU=(int)lroundf((row0*a.u+row1*b.u+row2*c.u)*uscale)-32768;
  int rowV=(int)lroundf((row0*a.v+row1*b.v+row2*c.v)*vscale)-32768;
  const int rearQ=bound((int)(rear*256+.5f),0,256);
  for(int y=top;y<bottom;++y){
    int e0=row0,e1=row1,e2=row2,u=rowU,v=rowV;
    uint16_t* dst=FB+(size_t)fbRow(y)*FB_W+left;
    for(int x=left;x<right;++x,++dst,e0+=dx0,e1+=dx1,e2+=dx2,u+=du,v+=dv){
      if(e0+bias0<0||e1+bias1<0||e2+bias2<0)continue;
      const int ix=u>>16,iy=v>>16,fx=(u>>8)&255,fy=(v>>8)&255;
      const int x0=bound(ix,0,f.width-1),x1=bound(ix+1,0,f.width-1);
      const int y0t=bound(iy,0,f.height-1)*f.width,y1t=bound(iy+1,0,f.height-1)*f.width;
      const int indices[4]={y0t+x0,y0t+x1,y1t+x0,y1t+x1};
      const int weights[4]={(256-fx)*(256-fy),fx*(256-fy),(256-fx)*fy,fx*fy};
      int alpha=0,r=0,g=0,blue=0;
      for(int k=0;k<4;++k){
        int weight=weights[k]*f.alpha[indices[k]];
        uint16_t color=f.colors[indices[k]];
        alpha+=weight;r+=((color>>11)&31)*weight;
        g+=((color>>5)&63)*weight;blue+=(color&31)*weight;
      }
      if(alpha<32768)continue;
      if(r*5>blue*6 && r*20>g*13){
        // Shift before multiplying to keep intermediates within signed 32 bits.
        r-=(r>>10)*rearQ;
        blue+=((r/5)>>8)*rearQ;
      }
      // gain is 256 for this renderer. Keep the parameter for S3 API parity.
      (void)gain;
      int rr=bound((r+alpha/2)/alpha,0,31);
      int gg=bound((g+alpha/2)/alpha,0,63);
      int bb=bound((blue+alpha/2)/alpha,0,31);
      *dst=blend565(*dst,(uint16_t)((rr<<11)|(gg<<5)|bb),(alpha+32768)>>16);
    }
    row0+=dy0;row1+=dy1;row2+=dy2;rowU+=duy;rowV+=dvy;
  }
}
static constexpr float meshU[]={0,.2f,.395f,.5f,.605f,.8f,1};
static constexpr float meshV[]={0,.23f,.30f,.38f,.46f,.54f,.76f,1};
static constexpr int MESH_COLS=sizeof(meshU)/sizeof(*meshU);
static constexpr int MESH_ROWS=sizeof(meshV)/sizeof(*meshV);
struct CachedMesh { Vertex vertices[MESH_ROWS][MESH_COLS]; bool ready; };
struct WingColumn { float x,span,sine,sineLag; };
static void buildMesh(const clione::Animal& a,Vertex (&mesh)[MESH_ROWS][MESH_COLS]) {
  const float size=a.size*FB_H,c=cosf(a.angle),s=sinf(a.angle);
  const float cyaw=cosf(a.yaw),syaw=sinf(a.yaw);
  const float bodyWidth=sqrtf(cyaw*cyaw+.6724f*syaw*syaw);
  const float wingPhase=clione::wingStroke(a).phase,amplitude=.68f+.24f*a.effort;
  const float extension=clione::extension(a),tailBend=.020f*sinf(wingPhase-.7f)-.08f*a.turn;
  auto column=[&](float x)->WingColumn {
    float span=fmaxf(0,(fabsf(x)-.105f)/.395f),side=x<0?-1:1;
    float phase=wingPhase-.7f*span+side*.035f;
    float sine=sinf(phase);
    return {side*.105f*bodyWidth+.80f*side*(fabsf(x)-.105f)*(.66f+.30f*cosf(phase))*cyaw
            +.16f*span*sine*syaw,span,sine,sinf(phase-.8f)};
  };
  WingColumn columns[MESH_COLS];
  for(int k=0;k<MESH_COLS;++k)columns[k]=column(meshU[k]-.5f);
  for(int j=0;j<MESH_ROWS;++j){
    float v=meshV[j];
    float headScale=(v>.04f && v<.26f)?1+.25f*extension*sinf((v-.04f)/.22f*3.14159265f):1;
    for(int k=0;k<MESH_COLS;++k){
      float x=(meshU[k]-.5f)*headScale,y=v-.5f;
      bool wing=v>=.23f && v<=.54f && fabsf(x)>.105f;
      if(wing){
        WingColumn col=headScale==1?columns[k]:column(x);
        x=col.x;
        y=-.18f+(y+.18f)*(1-.18f*col.span)
          +.192f*amplitude*col.span*col.sine
          +.04f*col.span*col.span*col.sineLag*(v-.30f)/.24f;
      }else{
        if(v>.54f){float tail=(v-.54f)/.46f;x+=tailBend*tail*tail;}
        x*=bodyWidth;
      }
      mesh[j][k]={a.x*FB_W+(c*x-s*y)*size,a.y*FB_H+(s*x+c*y)*size,meshU[k],v};
    }
  }
}
static CachedMesh cachedMeshes[clione::MAX_COUNT];
#else
static void triangle(Vertex a,Vertex b,Vertex c,int gain,float rear,int y0,int y1) {
  float area=edge(a,b,c.x,c.y); if(fabsf(area)<.00001f)return;
  if(area<0){Vertex temp=b;b=c;c=temp;area=-area;}
  int left=bound((int)floorf(fminf(a.x,fminf(b.x,c.x))),0,FB_W);
  int right=bound((int)ceilf(fmaxf(a.x,fmaxf(b.x,c.x))),0,FB_W);
  int top=bound((int)floorf(fminf(a.y,fminf(b.y,c.y))),y0,y1);
  int bottom=bound((int)ceilf(fmaxf(a.y,fmaxf(b.y,c.y))),y0,y1);
  if(left>=right||top>=bottom)return;
  const ClioneFrame& f=CLIONE_SWIM[2];
  bool ab=topLeft(a,b),bc=topLeft(b,c),ca=topLeft(c,a);
  float inv=1/area;
  for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
    float e0=edge(b,c,x+.5f,y+.5f),e1=edge(c,a,x+.5f,y+.5f),e2=edge(a,b,x+.5f,y+.5f);
    if(e0<0||(e0==0&&!bc)||e1<0||(e1==0&&!ca)||e2<0||(e2==0&&!ab))continue;
    float u=(e0*a.u+e1*b.u+e2*c.u)*inv*f.width-.5f;
    float v=(e0*a.v+e1*b.v+e2*c.v)*inv*f.height-.5f;
    int ix=(int)floorf(u),iy=(int)floorf(v);float fx=u-ix,fy=v-iy;
    float alpha=0,r=0,g=0,blue=0;
    for(int j=0;j<2;++j)for(int k=0;k<2;++k){
      size_t index=(size_t)bound(iy+j,0,f.height-1)*f.width+bound(ix+k,0,f.width-1);
      float weight=(k?fx:1-fx)*(j?fy:1-fy)*f.alpha[index];
      uint16_t color=f.colors[index];alpha+=weight;
      r+=((color>>11)&31)*weight;g+=((color>>5)&63)*weight;blue+=(color&31)*weight;
    }
    if(alpha<.5f)continue;
    // Orange organs remain visible through the translucent back, but subdued.
    if(r>blue*1.2f && r>g*.65f){r*=1-.25f*rear;blue+=r*.20f*rear;}
    float scale=gain/(256*alpha);
    uint16_t color=(bound((int)(r*scale+.5f),0,31)<<11)
                  |(bound((int)(g*scale+.5f),0,63)<<5)|bound((int)(blue*scale+.5f),0,31);
    blend(x,y,color,(int)(alpha+.5f));
  }
}
#endif
static void sprite(const clione::Animal& a,int index,int y0,int y1) {
#if !CONFIG_IDF_TARGET_ESP32C3
  static const float us[]={0,.1f,.2f,.3f,.395f,.5f,.605f,.7f,.8f,.9f,1};
  static const float vs[]={0,.23f,.30f,.38f,.46f,.54f,.64f,.76f,.9f,1};
  float size=a.size*FB_H,c=cosf(a.angle),s=sinf(a.angle);
#else
  float size=a.size*FB_H;
#endif
  if(a.y*FB_H+size*.8f<y0 || a.y*FB_H-size*.8f>=y1)return;
#if CONFIG_IDF_TARGET_ESP32C3
  auto& cache=cachedMeshes[index];
  auto& mesh=cache.vertices;
  if(!cache.ready){
    buildMesh(a,mesh);
    cache.ready=true;
  }
#else
  (void)index;
  const float wingPhase=clione::wingStroke(a).phase;
  static constexpr int MESH_ROWS=10,MESH_COLS=11;
  Vertex mesh[10][11];
  for(int j=0;j<10;++j)for(int k=0;k<11;++k)mesh[j][k]=pose(a,us[k],vs[j],size,c,s,wingPhase);
#endif
  const int gain=256;
  const int farSide=sinf(a.yaw)>=0?1:-1;
  const float rear=fmaxf(0,-cosf(a.yaw));
  // Far wing, torso, near wing: yaw changes actual overlap, not just width.
  for(int pass=0;pass<3;++pass)for(int j=0;j<MESH_ROWS-1;++j)for(int k=0;k<MESH_COLS-1;++k){
    int side=(j>=1&&j<=4)?(k<MESH_COLS/2-1?-1:(k>=MESH_COLS/2+1?1:0)):0;
    int layer=side==0?1:(side==farSide?0:2);if(layer!=pass)continue;
    triangle(mesh[j][k],mesh[j][k+1],mesh[j+1][k+1],gain,rear,y0,y1);
    triangle(mesh[j][k],mesh[j+1][k+1],mesh[j+1][k],gain,rear,y0,y1);
  }
}
// sin/cos of the six fixed cone spread angles, computed once at boot
// instead of per segment per animal.
struct ConeTrig { float s[6], c[6]; ConeTrig(){ const float a[3]={1.27f,.79f,.29f};
  for(int i=0;i<3;i++){ s[i]=-sinf(a[i]); c[i]=cosf(a[i]); s[3+i]=sinf(a[i]); c[3+i]=c[i]; } } };
static const ConeTrig CONE_TRIG;
static void cone(const clione::Animal& a,int index,float amount,int y0,int y1) {
  float w=a.size*FB_H,mx=a.x*FB_W,my=a.y*FB_H+clione::MOUTH_Y*w;
  int side=index<3?-1:1,pair=index%3;
  float length=w*(.14f+pair*.010f)*amount;
  float grip=clione::grasp(a),root=side*(.070f-pair*.025f)*w;
  // Per-animal rotation terms, hoisted out of the per-segment endpoint calls.
  const float c=cosf(a.angle),s=sinf(a.angle);
  const float squash=sqrtf(1-.3276f*sinf(a.yaw)*sinf(a.yaw));
  const float originX=a.x*FB_W,originY=a.y*FB_H;
  auto rotate=[&](float& x,float& y){
    float dx=(x-originX)*squash,dy=y-originY;
    x=originX+c*dx-s*dy;y=originY+s*dx+c*dy;
  };
  // Slightly stagger the three pairs; all six stay visibly distinct.
  float pulse=a.stage==clione::EXTRACT?.025f*sinf(a.age*5-pair*.7f):0;
  // Closed tips ring the held shell instead of meeting in one point.
  const float endX[3]={.062f,.066f,.036f},endY[3]={.050f,.095f,.135f};
  float openX=mx+root+CONE_TRIG.s[index]*length,openY=my-CONE_TRIG.c[index]*length;
  float tipX=openX+(mx+side*(endX[pair]+pulse*.25f)*w*amount-openX)*grip;
  float tipY=openY+(my-(endY[pair]+pulse)*w*amount-openY)*grip;
  float controlX=mx+root+CONE_TRIG.s[index]*length*(.55f+.60f*grip);
  float controlY=my-CONE_TRIG.c[index]*length*(.55f+.65f*grip);
  float px=mx+root,py=my;
  for(int k=1;k<=12;++k){
    float t=k/12.0f;
    float x=(1-t)*(1-t)*(mx+root)+2*(1-t)*t*controlX+t*t*tipX;
    float y=(1-t)*(1-t)*my+2*(1-t)*t*controlY+t*t*tipY;
    // Thick tapered cone: broad base, pointed tip.
    float radius=w*(.024f*(1-t)+.004f)*fminf(1,amount*4);
    float ax=px,ay=py,bx=x,by=y;rotate(ax,ay);rotate(bx,by);
    // Vivid red-orange, slightly paler toward the tip.
    line(ax,ay,bx,by,fmaxf(.3f,radius),rgb565((int)(222+28*t),(int)(66+70*t),(int)(34+40*t)),.92f,y0,y1);
    line(ax,ay,bx,by,fmaxf(.16f,radius*.35f),rgb565(255,178,128),.30f,y0,y1);
    px=x;py=y;
  }
}

}
void clionePrepareFrame() {
#if CONFIG_IDF_TARGET_ESP32C3
  for(auto& cache:cachedMeshes)cache.ready=false;
#endif
}
void clioneDrawBand(const clione::World& w,int y0,int y1) {
  y0=bound(y0,0,FB_H);y1=bound(y1,y0,FB_H);
  for(int i=0;i<clione::PREY_COUNT;++i)if(w.prey[i].active&&(w.prey[i].owner<0 || w.animals[w.prey[i].owner].stage<=clione::STRIKE))shell(w.prey[i],y0,y1);
  for(int i=w.count-1;i>=0;--i){
    const clione::Animal& a=w.animals[i];sprite(a,i,y0,y1);
    float ex=clione::extension(a);
    if(ex>0){
      float mx,my;clione::mouth(w,a,mx,my);mx*=FB_W;my*=FB_H;
      disc(mx,my,fmaxf(1.2f,a.size*FB_H*.045f)*ex,rgb565(92,35,38),.95f,y0,y1);
      for(int c=0;c<6;++c)cone(a,c,ex,y0,y1);
      // The held shell sits inside the ring of cone tips.
      if(a.prey>=0&&a.stage>=clione::HOLD&&a.stage<=clione::RETRACT){
        const clione::Prey& p=w.prey[a.prey];shell(p,y0,y1);
        if(a.stage==clione::EXTRACT){
          float progress=fminf(1,a.age/fmaxf(1,a.timer));
          // Soft tissue shortens toward the mouth; the shell stays external.
          line(mx,my-a.size*FB_H*.08f*(1-progress),mx,my,a.size*FB_H*.014f,
               rgb565(228,193,163),.7f*(1-progress),y0,y1);
        }
      }
    }
  }
}
