#!/usr/bin/perl

use strict;
use warnings;

my %F;
while (<>) {
  $F{$1}++ if (/\b(V7_ENABLE_\S+__\S+)\b/);
}

my $feature_defs = '';
foreach my $f(sort keys %F) {
  $feature_defs .= "#define $f 1\n";
}

print <<EOF
#if V7_BUILD_PROFILE == V7_BUILD_PROFILE_FULL

$feature_defs
#endif
EOF
