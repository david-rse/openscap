#!/usr/bin/env bash
. $builddir/tests/test_common.sh

set -e
set -o pipefail

name=$(basename $0 .sh)

result=$(mktemp -t ${name}.out.XXXXXX)
stderr=$(mktemp -t ${name}.err.XXXXXX)

$OSCAP xccdf eval --profile xccdf_moc.elpmaxe.www_profile_1 --results $result $srcdir/${name}.xccdf.xml 2> $stderr

echo "Stderr file = $stderr"
echo "Result file = $result"
[ -f $stderr ]; [ ! -s $stderr ]; rm $stderr

$OSCAP xccdf validate --skip-schematron $result

assert_exists 1 '//Profile'
assert_exists 2 '//Profile/select'
assert_exists 1 '//Profile/select[@selected="true"]'
assert_exists 1 '//Profile/select[@selected="false"]'
assert_exists 1 '//rule-result'
assert_exists 1 '//rule-result/result'
assert_exists 1 '//rule-result/result[text()="notselected"]'
assert_exists 1 '//score'
assert_exists 1 '//score[text()="0.000000"]'
rm $result
