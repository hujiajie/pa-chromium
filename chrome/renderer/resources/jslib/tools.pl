#!/usr/bin/perl

use File::Find;
use File::Copy;

my @files;
my @dirs = ".";
my $extention = ".bak";

find(sub { push(@files, $File::Find::name) if -f && !/\.bak$/ }, @dirs);

for (@files) {
    my $file = $_;
    my $bakfile = $file . $extention;

    if (-f && !/\.o$/) {
        print $file . "\n";
        copy $file, $bakfile or die "Copy failed: $!";
        open FO, $bakfile or die "Cannot open '$bakfile': $!";
        open FN, ">$file" or die "Cannot open '$file': $!";

        if (/\.(pl|[ch])$/) {
            while (<FO>) {
                s/[ \r]+$//;
                print FN $_ or die "write '$file' err: $!";
            }
        } else {
            while (<FO>) {
                s/[ \t\r]+$//;
                print FN $_ or die "write '$file' err: $!";
            }
        }

        close FN;
        close FO;
        unlink $bakfile;
    }
}
