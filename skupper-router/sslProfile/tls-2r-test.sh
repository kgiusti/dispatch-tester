#!/bin/bash
#

set -ex
set -o pipefail

CERTPATH=${CERTPATH:="/home/kgiusti/work/skupper/skupper-router/tests/ssl_certs"}
CPORT=${CPORT:=8888}
LPORT=${LPORT:=7777}
PAYLOAD=${PAYLOAD:=65007}
MAX_CYCLES=${MAX_CYCLES:=500}

openssl s_server -accept localhost:$CPORT -CAfile $CERTPATH/ca-certificate.pem --cert $CERTPATH/server-certificate.pem -key $CERTPATH/server-private-key.pem -pass "pass:server-password"  -ign_eof -Verify 10 -verify_return_error  -debug -trace < /dev/zero > /dev/null 2>&1 &
SERVER_PID=$!

sleep 3

function spawn_client {
    (printf "%*s" $PAYLOAD "?" | openssl s_client -connect localhost:$LPORT -CAfile $CERTPATH/ca-certificate.pem -cert $CERTPATH/client-certificate.pem -key $CERTPATH/client-private-key.pem -pass "pass:client-password" -quiet -no_ign_eof -verify_return_error  -verify 10) &
    CLIENT_PIDS+="$! "
}


CLIENT_PIDS=
for (( X=0 ; $X < $MAX_CYCLES ; X+=1 )); do

    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client

    skmanage update --type sslProfile --name listener-ssl-profile caCertFile=$CERTPATH/ca-certificate.pem certFile=$CERTPATH/server-certificate.pem privateKeyFile=$CERTPATH/server-private-key.pem password=server-password 
    #CLIENT_PIDS+="$! "

    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client

    skmanage update -r tls-router-B --type sslProfile --name connector-ssl-profile caCertFile=$CERTPATH/ca-certificate.pem certFile=$CERTPATH/client-certificate.pem privateKeyFile=$CERTPATH/client-private-key.pem password=client-password 
    # CLIENT_PIDS+="$! "

    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client
    spawn_client

    wait $CLIENT_PIDS
    CLIENT_PIDS=
done

kill $SERVER_PID
wait $SERVER_PID
echo $?


set +ex
set +o pipefail
