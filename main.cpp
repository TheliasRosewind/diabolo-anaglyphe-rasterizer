//
// Created by Alexis on 29/01/2020.
//

#define _USE_MATH_DEFINES
#include <cmath>
#include <limits>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include "vectors.h"
#include "model.h"
#include "tgaimage.h"

const int   width    = 800;
const int   height   = 800;
const int   depth    = 255;
const float sqt = 1.73;
const float pi = 3.14;
Model* model=NULL;

/**
 * Return a TGAColor from a Vec3f describing a color in the HSV system.
 */
TGAColor HSVToRGB(TGAColor color){
  float x = color[1]*std::cos(color[0]);
  float y = color[1]*std::sin(color[0]);
  return TGAColor(x*2./3+color[2],-x*1./3+y*1/sqt+color[2],-x*1./3-y*1./sqt+color[2]);
}

TGAColor multiplyColors(TGAColor base, TGAColor light){
  return TGAColor(base[2]*light[2]/255,base[1]*light[1]/255,base[0]*light[0]/255);
}

TGAColor addColors(TGAColor base, TGAColor add){
  return TGAColor(base[0]+add[0],base[1]+add[1],base[2]+add[2]);
}

/**
 * Transform .obj coordinates [-1,1] into discrete image coordinates [0,width|height]
 */
Vec3f CObjToImage(Vec3f v){
  int x0 = ((v.x+1.)*width/2.+.5);
  int y0 = ((v.y+1.)*height/2.+.5);
  float z0 = (v.z+1.)/2;
  return Vec3f(x0,y0,z0);
}

Vec2f CObjToImage(Vec2f v){
  int x0 = ((v.x+1.)*width/2.+.5);
  int y0 = ((v.y+1.)*height/2.+.5);
  return Vec2f(x0,y0);
}

Vec2f CObjToImage(Vec2f v, Vec2i clamp){
  int x0 = ((v.x+1.)*clamp.x/2.+.5);
  int y0 = ((v.y+1.)*clamp.y/2.+.5);
  return Vec2f(x0,y0);
}

/**
 * Convert 2D image coordinates[0,width|height] to 1D framebuffer coordinates [0,width*height]
 */
int CImageToZBuffer(int x, int y){
  return x*height+y;
}

int CImageToZBuffer(Vec3f v){
  return v.x*height+v.y;
}


/**
 * Draw a line between (x0,y0) and (x1,y1). Who could've guessed.
 */
void line(Vec2i p0, Vec2i p1, TGAImage *image, TGAColor color){
  int xFor = true;
  int x0=p0.x,y0=p0.y;
  int x1=p1.x,y1=p1.y;
  //Steepness check
  if(std::abs(x1-x0) < std::abs(y1-y0)){
    xFor=false;
    std::swap(x0,y0);
    std::swap(x1,y1);
  }
  //Forcing x0 <= x1
  if(x0>x1) {std::swap(x0,x1); std::swap(y0,y1);}
  //Line render
  for(int x=x0; x<=x1; x++){
    float t = (x-x0)/(float)(x1-x0);
    int y = y0 + (y1-y0)*t;
    //De-transpose if steep
    if(xFor){
      image->set(x,y,color);
    } else {
      image->set(y,x,color);
    }
  }
}

void line(Vec3f p0, Vec3f p1, TGAImage *image, TGAColor color){
  line(Vec2i(p0.x,p0.y),Vec2i(p1.x,p1.y),image,color);
}

/**
 * Draw a triangle, filled with given color.
 */
void bayesian_triangle(TexturedTriangle triangle, TGAImage *image, float *zbuffer, TGAColor color){
  Triangle3f tri = triangle.worldVertices;
  Triangle2f tex = triangle.textureVertices;
  Vec2f bbox_clamp(image->get_width()-1.,image->get_height()-1.);
  Vec2f bbox_min(std::numeric_limits<float>::max(),std::numeric_limits<float>::max());
  Vec2f bbox_max(std::numeric_limits<float>::min(),std::numeric_limits<float>::min());
  for(int i=0 ; i < 3 ; i++){
    for(int j=0; j < 2 ; j++){
      bbox_min[j]=std::max(0.f,std::min(bbox_min[j],tri[i][j]));
      bbox_max[j]=std::min(bbox_clamp[j],std::max(bbox_max[j],tri[i][j]));
    }
  }
  Vec3f v;
  for(v.x=bbox_min.x;v.x<=bbox_max.x;v.x++){
    for(v.y=bbox_min.y;v.y<=bbox_max.y;v.y++){
      Vec3f bc = tri.barycentric(v);
      if(bc.x<0||bc.y<0||bc.z<0) continue;
      v.z=0;
      for(int i=0;i<3;i++) { v.z += tri[i].z*bc[i]; }
      if(zbuffer[CImageToZBuffer(v)]<v.z){
	zbuffer[CImageToZBuffer(v)]=v.z;
	Vec2f ratios = Vec2f(tex.v0.x*bc[0]+tex.v1.x*bc[1]+tex.v2.x*bc[2],tex.v0.y*bc[0]+tex.v1.y*bc[1]+tex.v2.y*bc[2]);
	image->set(v.x,v.y,multiplyColors(model->getDiffuseAt(ratios),color));
      }
    }
  }
}

//DEPRECATED : Render a triangle using the old scanlines method.
void scanlines_triangle(Vec2i p0, Vec2i p1, Vec2i p2, TGAImage *image, TGAColor color){
  if(p0.y==p1.y && p0.y==p2.y) return;
  if(p0.y > p1.y) std::swap(p0,p1);
  if(p0.y > p2.y) std::swap(p0,p2);
  if(p1.y > p2.y) std::swap(p1,p2);
  int height = p2.y-p0.y;
  for(int y=0;y<height;y++){
    int lowerHalf = !(y > p1.y-p0.y || p1.y==p0.y);
    int segmentHeight = (lowerHalf ? p1.y-p0.y : p2.y-p1.y);
    float alpha = (float)y/height;
    float beta = (float)(y-(lowerHalf ? 0 : p1.y-p0.y))/segmentHeight;
    Vec2i A = p0 + (p2-p0) * alpha;
    Vec2i B = (lowerHalf ? p0 + (p1-p0) * beta : p1 + (p2-p1) * beta);
    if(A.x>B.x) std::swap(A,B);
    for(int x=A.x;x<B.x;x++){
      image->set(x,p0.y+y,color);
    }
  }
}

//Render a triangle in wireframe.
void wireframe(Triangle3f tri,TGAImage *image, TGAColor color){
  line(tri.v0,tri.v1,image,color);
  line(tri.v1,tri.v2,image,color);
  line(tri.v0,tri.v2,image,color);
}

//Convert a 3D vector into a 4D matrix.
Matrix vectorToMatrix(Vec3f vec){
  Matrix m(1,4);
  m[0][0]=vec.x;
  m[1][0]=vec.y;
  m[2][0]=vec.z;
  m[3][0]=1.f;
  return m;
}

//Convert a matrix back into a 3D vector.
Vec3f matrixToVector(Matrix m){
  return Vec3f(m[0][0]/m[3][0],m[1][0]/m[3][0],m[2][0]/m[3][0]);
}

//Create a viewport matrix at position (x,y), of size (w,h).
Matrix createViewport(int x, int y, int w, int h){
  Matrix m = Matrix::identity(4);
  m[0][0] = w/2.;
  m[0][3] = x+w/2.;
  m[1][1] = h/2.;
  m[1][3] = y+h/2.;
  m[2][2] = depth/2.;
  m[2][3] = depth/2.;
  return m;
}

/**
 * Rendering function.
 */
void render() {
  TGAImage* framebuffer[3];
  framebuffer[0] = new TGAImage(width,height,1);
  framebuffer[1] = new TGAImage(width,height,1);
  framebuffer[2] = new TGAImage(width,height,3);
  Vec3f light_dir(0,0,-1);
  /*Matrix projection = Matrix::identity(4);
  projection[3][2] = -1./3; //Place the camera at (0,0,-3)
  Matrix viewport = createViewport(width/8,height/8,width*3/4,height*3/4); cant make it work*/
  float zbuffer[2][width*height];
  for(int i=0;i<width*height;i++){
    zbuffer[0][i]=std::numeric_limits<float>::min();
    zbuffer[1][i]=std::numeric_limits<float>::min();
  }
  //Render the model
  for(int f=0;f<model->nfaces();f++){
    FaceData face = model->getFaceData(f);
    Vec3f screen_coords[2][3];
    Vec3f world_coords[2][3];
    Vec2f tex_coords[3];
    for(int i=0;i<3;i++){
      Vec3f v = model->getVertex(face.vertices[i]);
      Vec3f u = model->getVertex(face.vertices[i]);
      u.x-=0.1;//-(0.05*(u.z+1./2.)); //Closest vertices move more than farther away ones
      //screen_coords[i] = matrixToVector(viewport*projection*vectorToMatrix(v)); not working yay
      screen_coords[0][i] = CObjToImage(v);
      screen_coords[1][i] = CObjToImage(u);
      world_coords[0][i]=v;
      world_coords[1][i]=u;
      tex_coords[i] = model->getTextureVertex(face.texture_vertices[i]);
    }
    for(int i = 0 ; i < 2 ; i ++) {
      Vec3f n;
      n=cross(world_coords[i][2]-world_coords[i][0],world_coords[i][1]-world_coords[i][0]);
      n.normalize();
      float intensity = n*light_dir;
      if(intensity<=0) continue;
      TexturedTriangle tri(Triangle3f(screen_coords[i][0],screen_coords[i][1],screen_coords[i][2]),Triangle2f(tex_coords[0],tex_coords[1],tex_coords[2]));
      TGAColor color=TGAColor(255,255,255)*intensity;
      bayesian_triangle(tri,framebuffer[i],zbuffer[i],color);
    }
  }

  //Dump z-buffer and combine renders
  TGAImage *zbuffer_r = new TGAImage(width,height,3);
  for(int i = 0 ; i < width ; i++){
    for(int j = 0 ; j < height ; j++){
      zbuffer_r->set(i,j,TGAColor(255,255,255)*zbuffer[0][CImageToZBuffer(i,j)]);
      TGAColor color = TGAColor(framebuffer[0]->get(i,j)[0]*2,0,framebuffer[1]->get(i,j)[0]*2);
      framebuffer[2]->set(i,j,color);
    }
  }
  
  //Save the images
  framebuffer[2]->flip_vertically();
  zbuffer_r->flip_vertically();
  framebuffer[2]->write_tga_file("out.tga",0);
  zbuffer_r->write_tga_file("z.tga",0);
  
  delete model;
}

int main(int argc, char* argv[]) {
  if(argc==2){
    model = new Model(argv[1]);
  } else {
    model = new Model("diablo3_pose");
  }
  render();
  return 0;
}

