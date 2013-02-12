/* Convert xcf files to ppm
 *
 * This file was written by Henning Makholm <henning@makholm.net>
 * It is hereby in the public domain.
 * 
 * In jurisdictions that do not recognise grants of copyright to the
 * public domain: I, the author and (presumably, in those jurisdictions)
 * copyright holder, hereby permit anyone to distribute and use this code,
 * in source code or binary form, with or without modifications. This
 * permission is world-wide and irrevocable.
 *
 * Of course, I will not be liable for any errors or shortcomings in the
 * code, since I give it away without asking any compenstations.
 *
 * If you use or distribute this code, I would appreciate receiving
 * credit for writing it, in whichever way you find proper and customary.
 */

#include "xcftools.h"
#include "flatten.h"
#include "nlsini.h"
#include "options.h"
#include "version.h"

#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <getopt.h>

static int suppress_byline ;
static struct FlattenSpec flatspec ;
static FILE *outfile = NULL ;
static FILE *transfile = NULL ;

/*----------------------------------------------------------------------------*/
const struct option long_options[] = {
  option_help,
  option_version,
  option_verbose,
  option_bzip,
  option_gzip,
  option_unpack,
  option_output,
  option_alpha,
  option_background,
  option_force_alpha,
  option_color,
  option_colour,
  option_gray,
  option_grey,
  option_mono,
  option_pnm,
  option_truecolor,
  option_for_gif,
  option_dissolve,
  option_full_image,
  option_size,
  option_offset,
  option_autocrop,
  option_mode,
  option_percent,
  option_opacity,
  option_mask,
  option_nomask,
  option_utf8,
  { 0 }
};

/*----------------------------------------------------------------------------*/
const char* const short_options = short_options_prefix
  short_option_help
  short_option_version
  short_option_verbose
  short_option_bzip
  short_option_gzip
  short_option_unpack
  short_option_output
  short_option_alpha
  short_option_background
  short_option_force_alpha
  short_option_color
  short_option_gray
  short_option_mono
  short_option_pnm
  short_option_truecolor
  short_option_for_gif
  short_option_dissolve
  short_option_full_image
  short_option_size
  short_option_offset
  short_option_autocrop
  short_option_utf8
  ;

static void
start_writing(FILE **f,int version)
{
  const char *format[] = {"(format zero)",
                          "PBM-ascii",
                          "PGM-ascii",
                          "PPM-ascii",
                          "PBM",
                          "PGM",
                          "PPM" };

  if( verboseFlag )
    fprintf(stderr,f == &outfile ? gettext("Writing converted image as %s\n")
            : gettext("Writing transparency map as %s\n"),
            format[version]);
  
  *f = openout( f == &outfile ? flatspec.output_filename
                : flatspec.transmap_filename );
  fprintf(*f,"P%d",version);
  if( suppress_byline )
    ;
  else if( f == &outfile )
    fprintf(*f,gettext(" # Converted by xcf2pnm %s"), PACKAGE_STRING);
  else
    fprintf(*f,gettext(" # Transparency map by xcf2pnm %s"), PACKAGE_STRING);
  fprintf(*f,"\n%d %d\n%s",
          flatspec.dim.width,
          flatspec.dim.height,
          version == 4 ? "" : "255\n");
}

int
put_pbm_row(FILE *file,unsigned num,rgba *pixels,rgba mask) {
  unsigned out ;
  unsigned i ;
  int bitsleft = 8 ;
  out = 0 ;
  for( i=0; i<num; i++ ) {
    out <<= 1 ;
    if( (pixels[i] & mask) == 0 )
      out++ ; /* 1 is black */
    else if( (pixels[i] & mask) == mask )
      ; /* 0 is white */
    else
      return 0 ;
    if( --bitsleft == 0 ) {
      putc( out, file );
      out = 0 ;
      bitsleft = 8 ;
    }
  }
  if( bitsleft < 8 )
    putc( out << bitsleft, file );
  return 1 ;
}
    
static void
callback_common(unsigned num,rgba *pixels)
{
  if( flatspec.transmap_filename ) {
    if( flatspec.partial_transparency_mode == ALLOW_PARTIAL_TRANSPARENCY ) {
      unsigned i ;
      if( transfile == NULL ) start_writing(&transfile,5);
      for( i=0; i < num; i++ )
        putc( ALPHA(pixels[i]), transfile );
    } else {
      if( transfile == NULL ) {
        start_writing(&transfile,4);
      }
      /* Partial transparency should have been caught in the flattener,
       * so just extract a single byte.
       */
      put_pbm_row(transfile,num,pixels,(rgba)1 << ALPHA_SHIFT);
    }
  } else if( ALPHA(flatspec.default_pixel) < 128 ) {
    unsigned i ;
    for( i=0; i < num; i++ )
      if( !FULLALPHA(pixels[i]) )
        FatalGeneric
          (100, gettext("Transparency found, but -a option not given"));
  }
  xcffree(pixels) ;
}

static void
ppm_callback(unsigned num,rgba *pixels)
{
  unsigned i ;
  if( outfile == NULL ) start_writing(&outfile,6);
  for( i=0; i < num; i++ ) {
    putc( (pixels[i] >> RED_SHIFT) & 0xFF   , outfile );
    putc( (pixels[i] >> GREEN_SHIFT) & 0xFF , outfile );
    putc( (pixels[i] >> BLUE_SHIFT) & 0xFF  , outfile );
  }
  callback_common(num,pixels);
}

static void
pgm_callback(unsigned num,rgba *pixels)
{
  unsigned i ;
  if( outfile == NULL ) start_writing(&outfile,5);
  for( i=0; i < num; i++ ) {
    int gray = degrayPixel(pixels[i]) ;
    if( gray == -1 )
      FatalGeneric
        ( 103,
          gettext("Grayscale output selected, but colored pixel(s) found") );
    putc( gray, outfile );
  }
  callback_common(num,pixels);
}

static void
pbm_callback(unsigned num,rgba *pixels)
{
  if( outfile == NULL ) start_writing(&outfile,4);
  if( !put_pbm_row(outfile,num,pixels,
                   ((rgba)255 << RED_SHIFT) +
                   ((rgba)255 << GREEN_SHIFT) +
                   ((rgba)255 << BLUE_SHIFT)) )
    FatalGeneric
      ( 103,
        gettext( "Monochrome output selected, but not all pixels "
                 "are black or white" ) );
  callback_common(num,pixels);
}

static enum out_color_mode
guess_color_mode(const char *string)
{
  if( strlen(string) >= 3 ) {
    string += strlen(string)-3 ;
    if( strcmp(string,"ppm")==0 ) return COLOR_RGB ;
    if( strcmp(string,"pgm")==0 ) return COLOR_GRAY ;
    if( strcmp(string,"pbm")==0 ) return COLOR_MONO ;
  }
  return COLOR_BY_FILENAME ;
}

static lineCallback
selectCallback(void)
{
  if( flatspec.transmap_filename && ALPHA(flatspec.default_pixel) >= 128 )
    FatalGeneric(101,gettext("The -a option was given, "
                       "but the image has no transparency"));
  
  switch( flatspec.out_color_mode ) {
  default:
  case COLOR_RGB: return &ppm_callback ;
  case COLOR_GRAY: return &pgm_callback ;
  case COLOR_MONO: return &pbm_callback ;
  }
}

int
main(int argc,char **argv)
{
  struct ProcessControl process;

  setlocale(LC_ALL,"");
  progname = argv[0] ;
  nls_init();

  if( argc <= 1 ) gpl_blurb() ;
  
  init_flatspec(&flatspec) ;
  init_process_control( &process );

  flatspec.out_color_mode = COLOR_BY_FILENAME ;

  if ( option_parse
       ( argc, argv, short_options, long_options, &process, &flatspec ) )
    exit(1);

  // set the global flags
  verboseFlag = process.verboseFlag;
  use_utf8 = process.use_utf8;

  if( flatspec.out_color_mode == COLOR_BY_FILENAME &&
      strlen(flatspec.output_filename) > 4 &&
      flatspec.output_filename[strlen(flatspec.output_filename)-4] == '.' )
    flatspec.out_color_mode = guess_color_mode(flatspec.output_filename);

  /* If the output filename was not enough cue, see if we're running
   * through a symlink/hardlink that gives the required output format
   */
  if( flatspec.out_color_mode == COLOR_BY_FILENAME &&
      strlen(progname) > 3 )
    flatspec.out_color_mode = guess_color_mode(progname);
  
  if( flatspec.out_color_mode == COLOR_BY_FILENAME )
    flatspec.out_color_mode = COLOR_BY_CONTENTS ;
  
  read_or_mmap_xcf( process.inputFile, process.unzipper );
  getBasicXcfInfo() ;
  initColormap();
 
  complete_flatspec(&flatspec,NULL);
  if( flatspec.process_in_memory ) {
    rgba **allPixels = flattenAll(&flatspec);
    analyse_colormode(&flatspec,allPixels,NULL);
    shipoutWithCallback(&flatspec,allPixels,selectCallback());
  } else {
    flattenIncrementally(&flatspec,selectCallback());
  }
  closeout(outfile,flatspec.output_filename) ;
  closeout(transfile,flatspec.transmap_filename) ;
  return 0 ;
}
