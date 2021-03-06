#! /usr/bin/perl
# This short script extracts enum definitions from files stolen
# from the Gimp's sources.
#
# This file was written by Henning Makholm <henning@makholm.net>
# It is hereby in the public domain.
# 
# In jurisdictions that do not recognise grants of copyright to the
# public domain: I, the author and (presumably, in those jurisdictions)
# copyright holder, hereby permit anyone to distribute and use this code,
# in source code or binary form, with or without modifications. This
# permission is world-wide and irrevocable.
#
# Of course, I will not be liable for any errors or shortcomings in the
# code, since I give it away without asking any compenstations.
#
# If you use or distribute this code, I would appreciate receiving
# credit for writing it, in whichever way you find proper and customary.

use strict ; use warnings ;

print "/* Autogenerated from ",@ARGV," */\n" ;
print "#include \"enums.h\"\n" ;
print "#define N_\n" ;
print "#include <stdio.h>\n" ;
while(<>) {
    if( /^\s*typedef\s+enum\s/ ) {
        my @nodesc ;
        my @all ;
        my %desc ;
        my $enum ;
        $_ = <> ;
        /^\{\s*$/ or die "Expected opening brace" ;
        while( <> ) {
            if( s/^\s*,?(\w+)\s*(=\s*\d+)?,?\s*// ) {
                my $constant = $1 ;
                push @all, $constant ;
                if( m' desc="([^"]+)"' ) {
                    $desc{$constant} = $1 ;
                } else {
                    push @nodesc, $constant ;
                    $desc{$constant} = $constant ;
                }
            } elsif( /^\}\s*(\w+)\s*;/ ) {
                $enum = $1 ;
                last ;
            } else {
                die "Unparseable line [$_]" ;
            }
        }
        die "Unexpected EOF" unless defined $enum ;
        if( @nodesc ) {
            my $prefix = $nodesc[0] ;
            $prefix =~ s/.$// ;
            for( @desc{@nodesc} ) {
                while( substr($_,0,length $prefix) ne $prefix ) {
                    $prefix =~ s/.$// ;
                }
            }
            $prefix = length $prefix ;
            for( @desc{@nodesc} ) {
                $_ = substr($_,$prefix);
                $_ = "\u\L$_" ;
                s/_(.)/\U$1/g ;
                s/^Rle/RLE/;
            }
            my $suffix = substr($desc{$nodesc[0]},1) ;
            for( @desc{@nodesc} ) {
                while( substr($_,-length($suffix)) ne $suffix ) {
                    goto nosuffix if $suffix eq "" ;
                    $suffix =~ s/^.// ;
                }
            }
            $suffix = length $suffix ;
            $_ = substr($_,0,-$suffix) for @desc{@nodesc} ;
          nosuffix: ;
        }
        my $gettext = "" ;
        if( $enum ne "PropType" ) {
            $gettext = "N_" ;
        }
        my $buflen = 15 + length($enum);
        print "const char*\nshow$enum($enum x)\n{\n" ;
        print "  static char buf[$buflen];\n  switch(x) {\n" ;
        for my $c (@all) {
            print "    case $c: return $gettext(\"$desc{$c}\");\n" ;
        }
        print "    default: sprintf(buf,\"($enum:%d)\",(int)x);\n" ;
        print "             return buf;\n  }\n}\n";
    }
}
