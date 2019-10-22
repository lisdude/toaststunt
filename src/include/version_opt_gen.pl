#!/usr/bin/perl
use warnings;
use strict;

our $oreflect_file = 'version_options.h';

# @options grammar
#
# <option_list> ::= <option_elements> ...
# <option elements> ::=
#   _DDEF  => [<id> ...] |               # #define-only options
#   _DINT  => [<id> ...] |               # integer options
#   _DSTR  => [<id> ...] |               # string options
#   _DENUM => [[<id> => <ovalues>] ...]  # enumeration options
# <ovalues> ::=
#   <value> ... |   # abbreviates {<value>=>[], ...}
#   {<value> => <odescr> ...}
# <odescr> ::=
#   <id> |          # use <id> to depict this option value
#   [<option_list>] # use <value> AND include these other options
#   #
#   # there should probably be a way to both specify an <id>
#   # and include a list of suboptions, we don't need it yet

my @options =
  (
   #network settings
   _DENUM => [[NETWORK_PROTOCOL =>
	       { NP_SINGLE => [],
		 NP_TCP    => [_DINT => [qw(DEFAULT_PORT)]],
		 NP_LOCAL  => [_DSTR => [qw(DEFAULT_CONNECT_FILE)]],
	       }],
	      [NETWORK_STYLE =>
	       qw(NS_BSD
		  NS_SYSV
		)],
	      [MPLEX_STYLE =>
	       qw(MP_SELECT
		  MP_POLL
		  MP_FAKE
		)],
	      [OUTBOUND_NETWORK =>
	       { qw(0 OFF
		    1 ON
		  ) }],
	     ],
   _DINT => [qw(DEFAULT_CONNECT_TIMEOUT
		MAX_QUEUED_OUTPUT
		MAX_QUEUED_INPUT
	      )],

   # compatibility
   _DDEF => [qw(PLAYER_HUH
	      )],

   # optimizations
   _DDEF => [qw(UNFORKED_CHECKPOINTS
		BYTECODE_REDUCE_REF
		STRING_INTERNING
		MEMO_STRLEN
		MEMO_VALUE_BYTES
	      )],

   # input options
   _DDEF => [qw(LOG_COMMANDS
		INPUT_APPLY_BACKSPACE
		IGNORE_PROP_PROTECTED
	      )],
   _DSTR => [qw(OUT_OF_BAND_PREFIX
		OUT_OF_BAND_QUOTE_PREFIX
	      )],

   # execution limits
   _DINT => [qw(DEFAULT_MAX_STACK_DEPTH
		DEFAULT_FG_TICKS
		DEFAULT_BG_TICKS
		DEFAULT_FG_SECONDS
		DEFAULT_BG_SECONDS
		PATTERN_CACHE_SIZE
		DEFAULT_MAX_STRING_CONCAT
		MIN_STRING_CONCAT_LIMIT
		DEFAULT_MAX_LIST_VALUE_BYTES
		MIN_LIST_VALUE_BYTES_LIMIT
		DEFAULT_MAX_MAP_VALUE_BYTES
		MIN_MAP_VALUE_BYTES_LIMIT
	      )],
  );

our $indent;
my %put;

sub put_dlist {
   local $indent = shift;
   while ((my $_DXXX, my $lst, @_) = @_) {
      $put{$_DXXX}->($_) foreach (@$lst);
   }
}

$put{_DDEF} = sub {
   my ($n) = @_;
   print OREFL <<EOF ;
#${indent}ifdef $n
_DDEF("$n")
#${indent}else
_DNDEF("$n")
#${indent}endif
EOF
};

for (qw(_DSTR _DINT)) {
   my $_DXXX = $_;
   $put{$_} = sub {
      my ($n) = @_;
      print OREFL <<EOF ;
#${indent}ifdef $n
${_DXXX}1($n)
#${indent}else
_DNDEF("$n")
#${indent}endif
EOF
   };
}

$put{_DENUM} = sub {
   my($d,@svals) = @{$_[0]};
   print OREFL <<EOF ;
#${indent}if !defined($d)
_DNDEF("$d")
EOF
   my %svals = ref($svals[0]) ? %{$svals[0]} : (map {$_,[]} @svals);
   foreach my $vs (keys %svals) {
      my $vd = ref($svals{$vs}) ? $vs : $svals{$vs};
      print OREFL <<EOF ;
#${indent}elif $d == $vs
_DSTR("$d","$vd")
EOF
      put_dlist($indent . '  ', @{$svals{$vs}}) if ref($svals{$vs});
   }
   print OREFL <<EOF ;
#${indent}else
_DINT1($d)
#${indent}endif
EOF
};


open OREFL,">$oreflect_file" or die "could not write $oreflect_file: $!";
print OREFL <<EOF ;
/******************************************************************/
/*  Automatically generated file; changes made here may be lost   */
/*  If options.h changes, edit and rerun version_opt_gen.pl       */
/******************************************************************/
#define _DINT1(OPT) _DINT(#OPT,OPT)
#define _DSTR1(OPT) _DSTR(#OPT,OPT)
EOF
put_dlist('', @options);
close OREFL;

