#!/bin/bash
#
set -e
usage="Usage: $0 [-p <proton-branch>] [-d <dispatch-branch>] [-u <git-url>]"
usage+=" Use -P <proton-branch> -D <dispatch-branch> or"
usage+=" -U <git-url> for cross version testing"
usage+=" Example url for branches:"
usage+=" -d DISPATCH-2305 -u https://github.com/kgiusti/dispatch/archive/refs/heads"

while getopts "p:d:P:D:u:U:c:" opt; do
    case $opt in
        p)
            proton_branch=$OPTARG
            ;;
        d)
            dispatch_branch=$OPTARG
            ;;
        P)
            old_proton_branch=$OPTARG
            ;;
        D)
            old_dispatch_branch=$OPTARG
            ;;
        u)
            dispatch_url=$OPTARG
            ;;
        U)
            old_dispatch_url=$OPTARG
            ;;
        ?)
            echo $usage
            exit 1
            ;;
    esac
done

proton_branch=${proton_branch:-"main"}
old_proton_branch=${old_proton_branch:-$proton_branch}

dispatch_branch=${dispatch_branch:-"main"}
old_dispatch_branch=${old_dispatch_branch:-$dispatch_branch}

old_dispatch_url=${old_dispatch_url:-"https://github.com/apache/qpid-dispatch/archive"}
dispatch_url=${dispatch_url:-$old_dispatch_url}

set -x

podman build --tag dispatch-tester/edge1:1 --file ContainerFile \
       --build-arg config_file=Edge1.conf \
       --build-arg proton_branch=$old_proton_branch \
       --build-arg dispatch_branch=$old_dispatch_branch \
       --build-arg dispatch_url=$old_dispatch_url .

podman build --tag dispatch-tester/interiora:1 --file ContainerFile \
       --build-arg config_file=InteriorA.conf \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch \
       --build-arg dispatch_url=$dispatch_url .

podman build --tag dispatch-tester/interiorb:1 --file ContainerFile \
       --build-arg config_file=InteriorB.conf \
       --build-arg proton_branch=$old_proton_branch \
       --build-arg dispatch_branch=$old_dispatch_branch \
       --build-arg dispatch_url=$old_dispatch_url .

podman build --tag dispatch-tester/edge2:1 --file ContainerFile \
       --build-arg config_file=Edge2.conf \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch \
       --build-arg dispatch_url=$dispatch_url .

podman run -d --name InteriorA --net=host dispatch-tester/interiora:1
podman run -d --name InteriorB --net=host dispatch-tester/interiorb:1
podman run -d --name Edge1 --net=host dispatch-tester/edge1:1
podman run -d --name Edge2 --net=host dispatch-tester/edge2:1
set +x

