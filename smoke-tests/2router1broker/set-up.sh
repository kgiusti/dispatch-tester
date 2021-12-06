#!/bin/bash
#
set -e
usage="Usage: $0 [-p <proton-branch>] [-d <dispatch-branch>] [-u <git-url>] <artemis-tarfile>\n"
usage+=" Use -P <proton-branch> -D <dispatch-branch> or\n"
usage+=" -U <git-url> for cross version testing\n"
usage+=" This script uses 'podman' by default. This can be overridden using the CTOOL env var"

while getopts "p:d:P:D:u:U:" opt; do
    case $opt in
        p)
            proton_branch=$OPTARG
            ;;
        d)
            dispatch_branch=$OPTARG
            ;;
        P)
            old_proton=$OPTARG
            ;;
        D)
            old_dispatch=$OPTARG
            ;;
        u)
            dispatch_url=$OPTARG
            ;;
        U)
            old_dispatch_url=$OPTARG
            ;;
        ?)
            echo -e $usage
            exit 1
            ;;
    esac
done

shift $(($OPTIND - 1))
if [ $# -ne 1 ]; then
    echo "Error: artemis tar file not specified!"
    echo -e $usage
    exit 1
fi

CTOOL=${CTOOL:-podman}
proton_branch=${proton_branch:-"master"}
old_proton=${old_proton:-$proton_branch}

dispatch_branch=${dispatch_branch:-"master"}
dispatch_url=${dispatch_url:-"https://gitbox.apache.org/repos/asf/qpid-dispatch.git"}
old_dispatch=${old_dispatch:-$dispatch_branch}
old_dispatch_url=${old_dispatch_url:-$dispatch_url}

set -x

$CTOOL build --tag smoketest/broker:1 --file BrokerContainer --build-arg artemis_file=$1 .

$CTOOL build --tag smoketest/routera:1 --file RouterContainer \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch \
       --build-arg dispatch_url=$dispatch_url \
       --build-arg config_file=RouterA.conf .

$CTOOL build --tag smoketest/routerb:1 --file RouterContainer \
       --build-arg proton_branch=$old_proton \
       --build-arg dispatch_branch=$old_dispatch \
       --build-arg dispatch_url=$old_dispatch_url \
       --build-arg config_file=RouterB.conf .

$CTOOL run -d --name smoketest-broker  --net=host smoketest/broker:1
$CTOOL run -d --name smoketest-routerA --net=host smoketest/routera:1
$CTOOL run -d --name smoketest-routerB --net=host smoketest/routerb:1
set +x

