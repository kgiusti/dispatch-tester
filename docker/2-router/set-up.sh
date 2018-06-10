#!/bin/bash
#
set -e
usage="Usage: $0 [-p <proton-branch>] [-d <dispatch-branch>]"
usage+=" Use -P <proton-branch> and -D <dispatch-branch"
usage+=" for cross version testing"

while getopts "p:d:P:D:" opt; do
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
        ?)
            echo $usage
            return 1
            ;;
    esac
done

proton_branch=${proton_branch:-"master"}
dispatch_branch=${dispatch_branch:-"master"}
old_proton=${old_proton:-$proton_branch}
old_dispatch=${old_dispatch:-$dispatch_branch}

set -x
docker build --tag dispatch-tester/router1:1 --file Dockerfile-router1 \
       --build-arg proton_branch=$proton_branch \
       --build-arg dispatch_branch=$dispatch_branch .

docker build --tag dispatch-tester/router2:1 --file Dockerfile-router2 \
       --build-arg proton_branch=$old_proton \
       --build-arg dispatch_branch=$old_dispatch .

docker run -d --name Router1-2router --net=host dispatch-tester/router1:1
docker run -d --name Router2-2router --net=host dispatch-tester/router2:1 
set +x

