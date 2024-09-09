#!/usr/bin/env bash

. $builddir/tests/test_common.sh
set -e -o pipefail

echo "Test a signed SCAP source data stream with a valid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
result=$(mktemp)
$OSCAP xccdf eval --verbose INFO --verbose-log-file $verbose --results-arf $result $srcdir/simple_ds_valid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
grep -q "XML signature is valid." $stdout
grep -q "Validating XML signature" $verbose
grep -q "Signature is OK" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
assert_exists 1 '//TestResult/rule-result[@idref="xccdf_com.example.www_rule_test-pass"]/result[text()="pass"]'
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $result

echo "Test a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
result=$(mktemp)
$OSCAP xccdf eval --verbose INFO --verbose-log-file $verbose --results-arf $result $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $result ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Invalid signature in SCAP Source Datastream (1.3) content in $srcdir/simple_ds_invalid_sign.xml" $stderr
grep -q "Validating XML signature" $verbose
grep -q "Signature is invalid" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $result

echo "Test skipping signature validation on a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
result=$(mktemp)
$OSCAP xccdf eval --verbose INFO --verbose-log-file $verbose --skip-signature-validation --results-arf $result $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
! grep -q "XML signature is valid." $stdout
! grep -q "Validating XML signature" $verbose
assert_exists 1 '//TestResult/rule-result[@idref="xccdf_com.example.www_rule_test-pass"]/result[text()="pass"]'
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $result

echo "Test a signed SCAP source data stream with modified SCAP content"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
result=$(mktemp)
$OSCAP xccdf eval --verbose INFO --verbose-log-file $verbose --results-arf $result $srcdir/simple_ds_modified.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $result ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Invalid signature in SCAP Source Datastream (1.3) content in $srcdir/simple_ds_modified.xml" $stderr
grep -q "Validating XML signature" $verbose
grep -q "Signature is OK" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 1/2" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $result

echo "Test an unsigned SCAP source data stream (with signature enforced)"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
result=$(mktemp)
$OSCAP xccdf eval --verbose INFO --verbose-log-file $verbose --enforce-signature --results-arf $result $srcdir/simple_ds_no_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Signature not found" $stderr
grep -q "Validating XML signature" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $result

echo "Test a security guide generation from a signed SCAP source data stream with a valid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
html_guide=$(mktemp)
$OSCAP xccdf generate guide --verbose INFO --verbose-log-file $verbose --output $html_guide $srcdir/simple_ds_valid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
grep -q "XML signature is valid." $stdout
grep -q "Validating XML signature" $verbose
grep -q "Signature is OK" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
grep -q "<!DOCTYPE[^>]*html[^>]*>" $html_guide
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $html_guide

echo "Test a security guide generation from a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
html_guide=$(mktemp)
$OSCAP xccdf generate guide --verbose INFO --verbose-log-file $verbose --output $html_guide $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $html_guide ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Invalid signature in SCAP Source Datastream (1.3) content in $srcdir/simple_ds_invalid_sign.xml" $stderr
grep -q "Validating XML signature" $verbose
grep -q "Signature is invalid" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $html_guide

echo "Test skipping signature validation on a security guide generation from a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
html_guide=$(mktemp)
$OSCAP xccdf generate guide --verbose INFO --verbose-log-file $verbose --skip-signature-validation --output $html_guide $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
! grep -q "XML signature is valid." $stdout
! grep -q "Validating XML signature" $verbose
grep -q "<!DOCTYPE[^>]*html[^>]*>" $html_guide
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $html_guide

echo "Test a security guide generation from an unsigned SCAP source data stream (with signature enforced)"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
html_guide=$(mktemp)
$OSCAP xccdf generate guide --verbose INFO --verbose-log-file $verbose --enforce-signature --output $html_guide $srcdir/simple_ds_no_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $html_guide ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Signature not found" $stderr
grep -q "Validating XML signature" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $html_guide

echo "Test a fix generation from a signed SCAP source data stream with a valid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
fix_script=$(mktemp)
$OSCAP xccdf generate fix --verbose INFO --verbose-log-file $verbose --fix-type bash --output $fix_script $srcdir/simple_ds_valid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
grep -q "XML signature is valid." $stdout
grep -q "Validating XML signature" $verbose
grep -q "Signature is OK" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
grep -q "#!\(/usr\)\?/bin/\(env \)\?bash" $fix_script
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $fix_script

echo "Test a fix generation from a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
fix_script=$(mktemp)
$OSCAP xccdf generate fix --verbose INFO --verbose-log-file $verbose --fix-type bash --output $fix_script $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $fix_script ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Invalid signature in SCAP Source Datastream (1.3) content in $srcdir/simple_ds_invalid_sign.xml" $stderr
grep -q "Validating XML signature" $verbose
grep -q "Signature is invalid" $verbose
grep -q "SignedInfo references (ok/all): 3/3" $verbose
grep -q "Manifests references (ok/all): 2/2" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $fix_script

echo "Test skipping signature validation on a fix generation from a signed SCAP source data stream with an invalid signature"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
fix_script=$(mktemp)
$OSCAP xccdf generate fix --verbose INFO --verbose-log-file $verbose --skip-signature-validation --fix-type bash --output $fix_script $srcdir/simple_ds_invalid_sign.xml >$stdout 2>$stderr
! [ -s $stderr ]
! grep -q "XML signature is valid." $stdout
! grep -q "Validating XML signature" $verbose
grep -q "#!\(/usr\)\?/bin/\(env \)\?bash" $fix_script
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $fix_script

echo "Test a fix generation from an unsigned SCAP source data stream (with signature enforced)"
stdout=$(mktemp)
stderr=$(mktemp)
verbose=$(mktemp)
fix_script=$(mktemp)
$OSCAP xccdf generate fix --verbose INFO --verbose-log-file $verbose --enforce-signature --fix-type bash --output $fix_script $srcdir/simple_ds_no_sign.xml >$stdout 2>$stderr || ret=$?
[ $ret = 1 ]
[ -s $stderr ]
! [ -s $fix_script ]
! grep -q "XML signature is valid." $stdout
grep -q "OpenSCAP Error: Signature not found" $stderr
grep -q "Validating XML signature" $verbose
rm -f $stdout
rm -f $stderr
rm -f $verbose
rm -f $fix_script
