#!/usr/bin/env perl
# Reject a tracked source string literal that copies a 1.7.10 en_US value.
# Exceptions are exact file/value pairs in tools/lang_allow.tsv, with reasons.
use strict;
use warnings;
use bytes;
my ($lang, $allow) = @ARGV;
die "usage: langcheck.pl en_US.lang allow.tsv\n" unless $lang && $allow;
open my $lf, '<', $lang or die "$lang: $!";
my %value;
while (<$lf>) {
    s/\r?\n$//;
    next if /^\s*(?:#|$)/;
    my ($key, $text) = split /=/, $_, 2;
    $value{$text} = 1 if defined $text && length($text) >= 4;
}
close $lf;
open my $af, '<', $allow or die "$allow: $!";
my %allow;
while (<$af>) {
    chomp; next if /^\s*(?:#|$)/;
    my ($file, $text, $count, $why) = split /\t/, $_, 4;
    die "allow entry without a reason: $_\n" unless $file && $text && $count && $why;
    die "allow entry that is not a language value: $text\n" unless $value{$text};
    $allow{"$file\t$text"} = $count;
}
close $af;
open my $files, '-|', 'git', 'ls-files', '-z' or die "git ls-files: $!";
local $/ = "\0";
my (%seen, @bad);
while (my $file = <$files>) {
    chop $file;
    next unless $file =~ m{(?:^|/)(?:Makefile|Dockerfile)$|\.(?:c|h|cuh|cu|cc|cpp|hpp|m|mm|rs|js|ts|java|sh|bash|pl|mk|json|jsonl|yaml|yml|patch)$};
    open my $fh, '<', $file or die "$file: $!";
    local $/;
    my $src = <$fh>;
    close $fh;
    # Consume comments and character constants ahead of string literals.
    while ($src =~ m{(/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')}sg) {
        my $tok = $1;
        my $quote = substr($tok, 0, 1);
        next if $quote eq "'" && $file !~ m{(?:^|/)Makefile$|\.(?:sh|bash|pl|mk)$};
        next unless $quote eq '"' || $quote eq "'";
        my $text = substr($tok, 1, -1);
        if ($quote eq '"') {
            $text =~ s/\\(["\\])/$1/g;
            $text =~ s/\\n/\n/g;
            $text =~ s/\\r/\r/g;
            $text =~ s/\\t/\t/g;
            $text =~ s/\\([0-7]{1,3})/chr(oct($1))/ge;
        } else { $text =~ s/\\'/'/g; }
        next unless $value{$text};
        my $id = "$file\t$text";
        $seen{$id}++;
        push @bad, $id unless $allow{$id};
    }
}
close $files;
for my $id (sort keys %allow) { push @bad, "exception count\t$id: got " . ($seen{$id} // 0) . ", allowed $allow{$id}" if ($seen{$id} // 0) != $allow{$id}; }
if (@bad) { print "langcheck: $_\n" for @bad; exit 1; }
print "langcheck: PASS (", scalar(keys %seen), " justified file/value pairs)\n";
