# OBSOLETE after M1

This script reproduces the M0 fixture set against the legacy `OpenDepot`
repository. It requires a built `opendepot-cli.exe` from the `m0-baseline` tag
of the legacy repo. After M1 the CLI lives in this repository and the script
should call the local build instead.

Run from an environment where the legacy repo path is set:

    $env:LEGACY_OPENDEPOT = 'C:\Users\Rose\Projects\OpenDepot'
    ./generate_fixtures.ps1
