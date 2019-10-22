# use warnings;

# How to make texi2html put extra stuff into the <HEAD> portions of
# all of the html files.  I hope this file can be replaced by a
# commandline flag in the not-too-distant future.  --wrog

*T2H_InitGlobals_orig = \&T2H_InitGlobals;

*T2H_InitGlobals = sub {
   &T2H_InitGlobals_orig;
   $T2H_EXTRA_HEAD = '<link rel="stylesheet" href="common.css" type="text/css">';
}
