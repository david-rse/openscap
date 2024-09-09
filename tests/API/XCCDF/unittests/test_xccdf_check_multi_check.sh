#!/usr/bin/env bash
. $builddir/tests/test_common.sh

set -e
set -o pipefail

result=`mktemp`
stderr=`mktemp`

$OSCAP xccdf eval --results $result $srcdir/test_xccdf_check_multi_check.xccdf.xml 2> $stderr

echo "Stderr file = $stderr"
echo "Result file = $result"
[ -f $stderr ]; [ ! -s $stderr ]; rm $stderr

$OSCAP xccdf validate --skip-schematron $result

assert_exists 0 '//check[not(@multi-check)]'
assert_exists 1 '//Rule[@id="xccdf_moc.elpmaxe.www_rule_1"]/check[@multi-check="true"]'
assert_exists 1 '//Rule[@id="xccdf_moc.elpmaxe.www_rule_2"]/check[@multi-check="true"]'
assert_exists 1 '//Rule[@id="xccdf_moc.elpmaxe.www_rule_3"]/check[@multi-check="false"]'
assert_exists 1 '//Rule[@id="xccdf_moc.elpmaxe.www_rule_4"]/check[@multi-check="false"]'
rm $result
