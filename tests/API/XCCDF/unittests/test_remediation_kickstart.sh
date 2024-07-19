#!/usr/bin/env bash
. $builddir/tests/test_common.sh

set -e -o pipefail

function test_normal {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --profile common "$srcdir/test_remediation_kickstart.ds.xml"

    grep -q '# Kickstart for Common hardening profile' "$kickstart"
    grep -q 'services --disabled=telnet --enabled=auditd,rsyslog,sshd' "$kickstart"
    grep -q 'logvol /var/tmp --name=vartmp --vgname=system --size=1024' "$kickstart"
    grep -q 'mkdir /etc/scap' "$kickstart"
    grep -q '\-usbguard' "$kickstart"

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

function test_results_oriented {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --result-id xccdf_org.open-scap_testresult_xccdf_org.ssgproject.content_profile_ospp "$srcdir/test_remediation_kickstart.ds.xml" 2> "$stderr" || ret=$?

    [ $ret = 1 ]
    grep -q "It isn't possible to generate results-oriented Kickstarts." $stderr

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

function test_error_service {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --profile broken_service "$srcdir/test_remediation_kickstart_invalid.ds.xml" 2> "$stderr"

    grep -q "Unsupported 'service' command keyword 'slow' in command: 'service slow down'" "$stderr"

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

function test_error_package {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --profile broken_package "$srcdir/test_remediation_kickstart_invalid.ds.xml" 2> "$stderr"

    grep -q "Unsupported 'package' command keyword 'build' in command:'package build sources'" "$stderr"

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

function test_error_logvol {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --profile broken_logvol "$srcdir/test_remediation_kickstart_invalid.ds.xml" 2> "$stderr"

    grep -q "Unexpected string 'crypto' in command: 'logvol /var 158 crypto'" "$stderr"

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

function test_error_unknown {
    kickstart=$(mktemp)
    stderr=$(mktemp)

    $OSCAP xccdf generate fix --fix-type kickstart --output "$kickstart" --profile unknown_command "$srcdir/test_remediation_kickstart_invalid.ds.xml" 2> "$stderr"

    grep -q "Unsupported command keyword 'unknown' in command: 'unknown command'" "$stderr"

    rm -rf "$kickstart"
    rm -rf "$stderr"
}

test_normal
test_results_oriented
test_error_service
test_error_package
test_error_logvol
test_error_unknown
