
/*
 * Copyright (c) 2008 - 2009 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property and proprietary
 * rights in and to this software, related documentation and any modifications thereto.
 * Any use, reproduction, disclosure or distribution of this software and related
 * documentation without an express license agreement from NVIDIA Corporation is strictly
 * prohibited.
 *
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED *AS IS*
 * AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED,
 * INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE LIABLE FOR ANY
 * SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT
 * LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF
 * BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR
 * INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES
 */

//-------------------------------------------------------------------------------
//
//  tutorial
//
//-------------------------------------------------------------------------------

#include <optixu/optixpp_namespace.h>
#include <optixu/optixu_math_namespace.h>
#include <iostream>
#include <GLUTDisplay.h>
#include <ImageLoader.h>
#include "common.h"
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <math.h>

using namespace optix;

static float rand_range(float min, float max)
{
  return min + (max - min) * (float) rand() / (float) RAND_MAX;
}


//-----------------------------------------------------------------------------
// 
// Whitted Scene
//
//-----------------------------------------------------------------------------

class Tutorial : public SampleScene
{
public:
  Tutorial(const std::string& texture_path)
    : SampleScene(), m_width(1080u), m_height(720u), texture_path( texture_path )
  {}
  
  // From SampleScene
  void   initScene( InitialCameraData& camera_data );
  void   trace( const RayGenCameraData& camera_data );
  void   doResize( unsigned int width, unsigned int height );
  void   setDimensions( const unsigned int w, const unsigned int h ) { m_width = w; m_height = h; }
  Buffer getOutputBuffer();

private:
  std::string texpath( const std::string& base );
  void createGeometry();

  unsigned int m_width;
  unsigned int m_height;
  std::string   texture_path;
  std::string  m_ptx_path;
};

void Tutorial::initScene( InitialCameraData& camera_data )
{
  // set up path to ptx file
  m_ptx_path = std::string("H:\\Prog\\Optix\\Optix2000\\vc10\\ptx\\raytrace.cu.ptx");

  // context 
  m_context->setRayTypeCount( 2 );
  m_context->setEntryPointCount( 1 );
  m_context->setStackSize( 4640 );

  m_context["max_depth"]->setInt(100);
  m_context["radiance_ray_type"]->setUint(0);
  m_context["shadow_ray_type"]->setUint(1);
  m_context["frame_number"]->setUint( 0u );
  m_context["scene_epsilon"]->setFloat( 1.e-3f );
  m_context["importance_cutoff"]->setFloat( 0.01f );
  m_context["ambient_light_color"]->setFloat( 0.31f, 0.33f, 0.28f );

  m_context["output_buffer"]->set( createOutputBuffer(RT_FORMAT_UNSIGNED_BYTE4, m_width, m_height) );

  // Ray gen program
  std::string camera_name = "pinhole_camera";
  Program ray_gen_program = m_context->createProgramFromPTXFile( m_ptx_path, camera_name );
  m_context->setRayGenerationProgram( 0, ray_gen_program );

  // Exception / miss programs
  Program exception_program = m_context->createProgramFromPTXFile( m_ptx_path, "exception" );
  m_context->setExceptionProgram( 0, exception_program );
  m_context["bad_color"]->setFloat( 0.0f, 1.0f, 0.0f );

  std::string miss_name = "miss";
  m_context->setMissProgram( 0, m_context->createProgramFromPTXFile( m_ptx_path, miss_name ) );
  const float3 default_color = make_float3(1.0f, 1.0f, 1.0f);
  m_context["envmap"]->setTextureSampler( loadTexture( m_context, texpath("CedarCity.hdr"), default_color) );
  m_context["bg_color"]->setFloat( make_float3( 0.34f, 0.55f, 0.85f ) );

  // Lights
  BasicLight lights[] = { 
    { make_float3( -5.0f, 60.0f, -16.0f ), make_float3( 1.0f, 1.0f, 1.0f ), 1 }
  };

  Buffer light_buffer = m_context->createBuffer(RT_BUFFER_INPUT);
  light_buffer->setFormat(RT_FORMAT_USER);
  light_buffer->setElementSize(sizeof(BasicLight));
  light_buffer->setSize( sizeof(lights)/sizeof(lights[0]) );
  memcpy(light_buffer->map(), lights, sizeof(lights));
  light_buffer->unmap();

  m_context["lights"]->set(light_buffer);

  // Set up camera
  camera_data = InitialCameraData( make_float3( 7.0f, 9.2f, -6.0f ), // eye
                                   make_float3( 0.0f, 4.0f,  0.0f ), // lookat
                                   make_float3( 0.0f, 1.0f,  0.0f ), // up
                                   60.0f );                          // vfov

  m_context["eye"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
  m_context["U"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
  m_context["V"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );
  m_context["W"]->setFloat( make_float3( 0.0f, 0.0f, 0.0f ) );

  // 3D solid noise buffer, 1 float channel, all entries in the range [0.0, 1.0].
  srand(0); // Make sure the pseudo random numbers are the same every run.

  int tex_width  = 64;
  int tex_height = 64;
  int tex_depth  = 64;
  Buffer noiseBuffer = m_context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT, tex_width, tex_height, tex_depth);
  float *tex_data = (float *) noiseBuffer->map();
  
  // Random noise in range [0, 1]
  for (int i = tex_width * tex_height * tex_depth;  i > 0; i--) {
    // One channel 3D noise in [0.0, 1.0] range.
    *tex_data++ = rand_range(0.0f, 1.0f);
  }
  noiseBuffer->unmap(); 


  // Noise texture sampler
  TextureSampler noiseSampler = m_context->createTextureSampler();

  noiseSampler->setWrapMode(0, RT_WRAP_REPEAT);
  noiseSampler->setWrapMode(1, RT_WRAP_REPEAT);
  noiseSampler->setFilteringModes(RT_FILTER_LINEAR, RT_FILTER_LINEAR, RT_FILTER_NONE);
  noiseSampler->setIndexingMode(RT_TEXTURE_INDEX_NORMALIZED_COORDINATES);
  noiseSampler->setReadMode(RT_TEXTURE_READ_NORMALIZED_FLOAT);
  noiseSampler->setMaxAnisotropy(1.0f);
  noiseSampler->setMipLevelCount(1);
  noiseSampler->setArraySize(1);
  noiseSampler->setBuffer(0, 0, noiseBuffer);

  m_context["noise_texture"]->setTextureSampler(noiseSampler);

  // Populate scene hierarchy
  createGeometry();

  // Prepare to run
  m_context->validate();
  m_context->compile();
}


Buffer Tutorial::getOutputBuffer()
{
  return m_context["output_buffer"]->getBuffer();
}


void Tutorial::trace( const RayGenCameraData& camera_data )
{
  m_context["eye"]->setFloat( camera_data.eye );
  m_context["U"]->setFloat( camera_data.U );
  m_context["V"]->setFloat( camera_data.V );
  m_context["W"]->setFloat( camera_data.W );

  Buffer buffer = m_context["output_buffer"]->getBuffer();
  RTsize buffer_width, buffer_height;
  buffer->getSize( buffer_width, buffer_height );

  m_context->launch( 0, static_cast<unsigned int>(buffer_width),
                      static_cast<unsigned int>(buffer_height) );
}


void Tutorial::doResize( unsigned int width, unsigned int height )
{
  // output buffer handled in SampleScene::resize
}

std::string Tutorial::texpath( const std::string& base )
{
  return texture_path + "/" + base;
}

float4 make_plane( float3 n, float3 p )
{
  n = normalize(n);
  float d = -dot(n, p);
  return make_float4( n, d );
}

void Tutorial::createGeometry()
{
  std::string box_ptx("H:\\Prog\\Optix\\Optix2000\\vc10\\ptx\\box.cu.ptx");
  Program box_bounds = m_context->createProgramFromPTXFile( box_ptx, "box_bounds" );
  Program box_intersect = m_context->createProgramFromPTXFile( box_ptx, "box_intersect" );

  // Create box
  Geometry box = m_context->createGeometry();
  box->setPrimitiveCount( 1u );
  box->setBoundingBoxProgram( box_bounds );
  box->setIntersectionProgram( box_intersect );
  box["boxmin"]->setFloat( -2.0f, 0.0f, -2.0f );
  box["boxmax"]->setFloat(  2.0f, 7.0f,  2.0f );

  // Create chull
  Geometry chull = 0;

  // Floor geometry
  std::string pgram_ptx("H:\\Prog\\Optix\\Optix2000\\vc10\\ptx\\parallelogram.cu.ptx");
  Geometry parallelogram = m_context->createGeometry();
  parallelogram->setPrimitiveCount( 1u );
  parallelogram->setBoundingBoxProgram( m_context->createProgramFromPTXFile( pgram_ptx, "bounds" ) );
  parallelogram->setIntersectionProgram( m_context->createProgramFromPTXFile( pgram_ptx, "intersect" ) );
  float3 anchor = make_float3( -64.0f, 0.01f, -64.0f );
  float3 v1 = make_float3( 128.0f, 0.0f, 0.0f );
  float3 v2 = make_float3( 0.0f, 0.0f, 128.0f );
  float3 normal = cross( v2, v1 );
  normal = normalize( normal );
  float d = dot( normal, anchor );
  v1 *= 1.0f/dot( v1, v1 );
  v2 *= 1.0f/dot( v2, v2 );
  float4 plane = make_float4( normal, d );
  parallelogram["plane"]->setFloat( plane );
  parallelogram["v1"]->setFloat( v1 );
  parallelogram["v2"]->setFloat( v2 );
  parallelogram["anchor"]->setFloat( anchor );

  // Materials
  std::string box_chname = "closest_hit_radiance0";
  
  Material box_matl = m_context->createMaterial();
  Program box_ch = m_context->createProgramFromPTXFile( m_ptx_path, box_chname );
  box_matl->setClosestHitProgram( 0, box_ch );
  box_matl["Ka"]->setFloat( 0.3f, 0.3f, 0.3f );
  box_matl["Kd"]->setFloat( 0.6f, 0.7f, 0.8f );
  box_matl["Ks"]->setFloat( 0.8f, 0.9f, 0.8f );
  box_matl["phong_exp"]->setFloat( 88 );
  box_matl["reflectivity_n"]->setFloat( 0.2f, 0.2f, 0.2f );

  std::string floor_chname = "closest_hit_radiance0";
    
  Material floor_matl = m_context->createMaterial();
  Program floor_ch = m_context->createProgramFromPTXFile( m_ptx_path, floor_chname );
  floor_matl->setClosestHitProgram( 0, floor_ch );
  floor_matl["Ka"]->setFloat( 0.3f, 0.3f, 0.1f );
  floor_matl["Kd"]->setFloat( 194/255.f*.6f, 186/255.f*.6f, 151/255.f*.6f );
  floor_matl["Ks"]->setFloat( 0.4f, 0.4f, 0.4f );
  floor_matl["reflectivity"]->setFloat( 0.1f, 0.1f, 0.1f );
  floor_matl["reflectivity_n"]->setFloat( 0.05f, 0.05f, 0.05f );
  floor_matl["phong_exp"]->setFloat( 88 );
  floor_matl["tile_v0"]->setFloat( 0.25f, 0, .15f );
  floor_matl["tile_v1"]->setFloat( -.15f, 0, 0.25f );
  floor_matl["crack_color"]->setFloat( 0.1f, 0.1f, 0.1f );
  floor_matl["crack_width"]->setFloat( 0.02f );

  // Glass material
  Material glass_matl;
  if( chull.get() ) {
    Program glass_ch = m_context->createProgramFromPTXFile( m_ptx_path, "glass_closest_hit_radiance" );
    std::string glass_ahname = "any_hit_shadow";
    Program glass_ah = m_context->createProgramFromPTXFile( m_ptx_path, glass_ahname );
    glass_matl = m_context->createMaterial();
    glass_matl->setClosestHitProgram( 0, glass_ch );
    glass_matl->setAnyHitProgram( 1, glass_ah );

    glass_matl["importance_cutoff"]->setFloat( 1e-2f );
    glass_matl["cutoff_color"]->setFloat( 0.34f, 0.55f, 0.85f );
    glass_matl["fresnel_exponent"]->setFloat( 3.0f );
    glass_matl["fresnel_minimum"]->setFloat( 0.1f );
    glass_matl["fresnel_maximum"]->setFloat( 1.0f );
    glass_matl["refraction_index"]->setFloat( 1.4f );
    glass_matl["refraction_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["reflection_color"]->setFloat( 1.0f, 1.0f, 1.0f );
    glass_matl["refraction_maxdepth"]->setInt( 100 );
    glass_matl["reflection_maxdepth"]->setInt( 100 );
    float3 extinction = make_float3(.80f, .89f, .75f);
    glass_matl["extinction_constant"]->setFloat( log(extinction.x), log(extinction.y), log(extinction.z) );
    glass_matl["shadow_attenuation"]->setFloat( 0.4f, 0.7f, 0.4f );
  }

  // Create GIs for each piece of geometry
  std::vector<GeometryInstance> gis;
  gis.push_back( m_context->createGeometryInstance( box, &box_matl, &box_matl+1 ) );
  gis.push_back( m_context->createGeometryInstance( parallelogram, &floor_matl, &floor_matl+1 ) );
  if(chull.get())
    gis.push_back( m_context->createGeometryInstance( chull, &glass_matl, &glass_matl+1 ) );

  // Place all in group
  GeometryGroup geometrygroup = m_context->createGeometryGroup();
  geometrygroup->setChildCount( static_cast<unsigned int>(gis.size()) );
  geometrygroup->setChild( 0, gis[0] );
  geometrygroup->setChild( 1, gis[1] );
  if(chull.get())
    geometrygroup->setChild( 2, gis[2] );
  geometrygroup->setAcceleration( m_context->createAcceleration("NoAccel","NoAccel") );

  m_context["top_object"]->set( geometrygroup );
  m_context["top_shadower"]->set( geometrygroup );
}


//-----------------------------------------------------------------------------
//
// Main driver
//
//-----------------------------------------------------------------------------

int main( int argc, char** argv )
{
  GLUTDisplay::init( argc, argv );

  unsigned int width = 1080u, height = 720u;

  std::string texture_path = std::string( sutilSamplesDir() ) + "/tutorial/data";
  std::string title("Test");
  try{
	Tutorial scene(texture_path);
	scene.setDimensions( width, height );
	GLUTDisplay::run( title, &scene );
  }
  catch( Exception& e ){
	sutilReportError( e.getErrorString().c_str() );
	exit(1);
  }
  return 0;
}
